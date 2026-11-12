/* Radio key-up / key-down sounds, synthesised (no recorded audio on the device).
 *
 * Modelled on measurements of 18 reference radio sounds (tools/voice_analysis/keys_report.md):
 * every one is a sharp click that excites a short ring (dies 20 dB in 10-60 ms) in one of
 * three pitch groups, wrapped in band-limited radio noise (about 600 Hz - 3 kHz):
 *     low "tock"   ~ 340 / 800-1050 Hz
 *     mid "tick"   ~ 1380-1700 Hz
 *     high "chirp" ~ 2150-2650 Hz
 * Key-up:   short static burst -> click + ring.
 * Key-down: squelch hiss tail -> brief gap -> click + ring.
 * Every press picks a group and jitters pitch / decay / levels, so it never repeats exactly.
 * Pure C, float, no allocation: runs per sample in the audio task. */
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include "keysnd.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct { float b0, b1, b2, a1, a2, z1, z2; } bq_t;

static void bq_pass(bq_t *q, float f0, bool high, int rate)
{
    float w = 2.0f * (float)M_PI * f0 / rate, c = cosf(w), s = sinf(w), al = s / (2.0f * 0.707f), a0 = 1 + al;
    if (high) { q->b0 = (1 + c) / 2 / a0; q->b1 = -(1 + c) / a0; }
    else      { q->b0 = (1 - c) / 2 / a0; q->b1 = (1 - c) / a0; }
    q->b2 = q->b0; q->a1 = -2 * c / a0; q->a2 = (1 - al) / a0;
    q->z1 = q->z2 = 0;
}

static void bq_bandpass(bq_t *q, float f0, float qf, int rate)
{
    float w = 2.0f * (float)M_PI * f0 / rate, c = cosf(w), s = sinf(w), al = s / (2.0f * qf), a0 = 1 + al;
    q->b0 = al / a0; q->b1 = 0; q->b2 = -al / a0;
    q->a1 = -2 * c / a0; q->a2 = (1 - al) / a0;
    q->z1 = q->z2 = 0;
}

static inline float bq_run(bq_t *q, float x)
{
    float y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2;
    q->z2 = q->b2 * x - q->a2 * y;
    return y;
}

static uint32_t s_rng = 0x5151AB;
static inline float rnd01(void) { s_rng = s_rng * 1664525u + 1013904223u; return (s_rng >> 8) / 16777216.0f; }
static inline float noise(void) { return rnd01() * 2.0f - 1.0f; }
static inline float jitter(float lo, float hi) { return lo + (hi - lo) * rnd01(); }

static int s_rate = 16000;
static struct {
    int n, len;            /* sample counter, total length */
    int click_at;          /* sample of the click */
    int hiss_end;          /* key-down: squelch tail ends here (then a gap) */
    float vol, burst_amp, hiss_amp, click_amp;
    float ring_decay;      /* per-sample envelope multiplier of the ring */
    float ring_env;
    bq_t ring, band, band2;
    bq_t lp1, lp2, hp;     /* the radio's own band limit on everything: ~450 Hz - 3 kHz */
} K;

void keysnd_init(int rate_hz) { s_rate = rate_hz; K.n = K.len = 0; }

bool keysnd_active(void) { return K.n < K.len; }

void keysnd_start(bool keyup, float volume)
{
    static int last_group = -1;
    int g;
    do { g = (int)(rnd01() * 3.0f); } while (g == last_group && rnd01() < 0.7f);   /* mostly vary */
    last_group = g;
    float f;
    switch (g) {
    case 0:  f = rnd01() < 0.12f ? jitter(320, 360) : jitter(800, 1050); break;   /* low tock  */
    case 1:  f = jitter(1380, 1700); break;                                       /* mid tick  */
    default: f = jitter(2150, 2650); break;                                       /* high chirp */
    }
    if (f > s_rate * 0.45f) f = s_rate * 0.45f;
    float t20 = jitter(0.015f, 0.055f);                 /* ring falls 20 dB in 15-55 ms */
    float tau = t20 / 2.303f;
    K.ring_decay = expf(-1.0f / (tau * s_rate));
    bq_bandpass(&K.ring, f, f * (float)M_PI * tau, s_rate);   /* Q matched to the decay */
    float nc = 0.35f * 1300.0f + 0.65f * (f < 600 ? 700.0f : f);   /* noise tinted toward the ring */
    bq_bandpass(&K.band, nc, 0.8f, s_rate);                   /* radio noise band ~600 Hz - 3 kHz */
    bq_bandpass(&K.band2, nc, 0.8f, s_rate);
    bq_pass(&K.lp1, jitter(2300, 2900), false, s_rate);
    bq_pass(&K.lp2, 3200, false, s_rate);
    bq_pass(&K.hp, 450, true, s_rate);
    K.vol = volume;
    K.click_amp = jitter(0.8f, 1.0f);
    K.burst_amp = jitter(0.12f, 0.35f);
    K.ring_env = 0;
    if (keyup) {
        K.click_at = (int)(s_rate * jitter(0.010f, 0.035f));      /* short static burst first */
        K.hiss_end = 0;
        K.hiss_amp = 0;
    } else {
        K.hiss_end = (int)(s_rate * jitter(0.040f, 0.090f));      /* squelch tail ... */
        K.click_at = K.hiss_end + (int)(s_rate * jitter(0.012f, 0.030f));   /* ... gap, click */
        K.hiss_amp = jitter(0.10f, 0.25f);
    }
    K.len = K.click_at + (int)(s_rate * (t20 * 1.6f + 0.03f));
    K.n = 0;
}

float keysnd_next(void)
{
    if (K.n >= K.len) return 0.0f;
    int n = K.n++;
    float out = 0;
    /* key-down squelch tail: steady band-limited hiss that stops abruptly */
    if (n < K.hiss_end) out += bq_run(&K.band2, noise()) * K.hiss_amp * 3.0f;
    /* key-up static: a rising burst of radio noise leading into the click */
    if (K.hiss_end == 0 && n < K.click_at) {
        float a = (float)n / (K.click_at + 1);
        out += bq_run(&K.band2, noise()) * K.burst_amp * a * 2.5f;
    }
    /* the click: a 1-2 sample impulse + a few ms of crackle, which also rings the resonator */
    float exc = 0;
    if (n == K.click_at) exc = K.click_amp;
    else if (n == K.click_at + 1) exc = -0.6f * K.click_amp;
    int since = n - K.click_at;
    if (since >= 0 && since < s_rate / 250) exc += noise() * 0.35f * K.click_amp * (1.0f - since * 250.0f / s_rate);
    if (since >= 0) {
        float ring = bq_run(&K.ring, exc) * 6.0f;                 /* resonator rings at its own decay */
        float crack = bq_run(&K.band, exc + noise() * 0.02f);
        float tail = since < s_rate / 40 ? 1.0f - since * 40.0f / s_rate : 0.0f;   /* 25 ms static fall */
        out += ring + exc * 0.5f + crack * K.burst_amp * 1.5f + bq_run(&K.band2, noise()) * K.burst_amp * tail;
    }
    out = bq_run(&K.hp, bq_run(&K.lp2, bq_run(&K.lp1, out))) * 1.6f;
    if (out > 1.0f) out = 1.0f;
    if (out < -1.0f) out = -1.0f;
    return out * K.vol;
}
