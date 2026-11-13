/* The trooper voice chain (see voicefx.h). Same radio effect math as firmware3/main/audio.c
 * (band-pass 300 Hz - 3 kHz, soft-clip drive, bitcrush, static), now with the learned voice
 * shift in front and synthesised key sounds at the gate edges.
 *
 * Gate: VOX listens to the raw mic (no delay), so the gate opens the moment you speak; with
 * the shift on, the shifted voice arrives 32 ms later, so the first syllable is never cut.
 * The key-up sound plays over the start of the transmission (not before it), the key-down
 * sound after the 350 ms hang. */
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include "voicefx.h"
#include "keysnd.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int s_rate = 16000;
#define POST_MAKEUP 1.3f             /* level back after the post-drive cut */
#define RADIO_MAKEUP 2.0f            /* the thin band loses level: bring it back before the drive */
static voicefx_params_t P = {70, 22, 15, 60, false, 1.0f, 1.0f, 0.0f, 1.0f, 0, 50};
static float s_gain = 1.0f, s_knee = 0.7f;

typedef struct { float b0, b1, b2, a1, a2, z1, z2; } biquad_t;
/* radio voicing, matched to the trooper references (tools/voice_analysis/compare.py):
 * steep cut below ~700 Hz, presence lifts at 1.7 and 2.5 kHz, steep cut above ~3.2 kHz */
static biquad_t s_hpf, s_lpf, s_hpf2, s_lpf2, s_pk1, s_pk2, s_round, s_post_hp, s_post_hp2, s_post_pk;          /* s_round: smooths the clipped tops (SOFT CLIP) */

static void biquad_set(biquad_t *q, float f0, bool highpass)
{
    float w = 2.0f * (float)M_PI * f0 / s_rate;
    float c = cosf(w), s = sinf(w), alpha = s / (2.0f * 0.707f);
    float a0 = 1 + alpha;
    if (highpass) { q->b0 = (1 + c) / 2 / a0; q->b1 = -(1 + c) / a0; q->b2 = q->b0; }
    else          { q->b0 = (1 - c) / 2 / a0; q->b1 = (1 - c) / a0;  q->b2 = q->b0; }
    q->a1 = (-2 * c) / a0; q->a2 = (1 - alpha) / a0;
    q->z1 = q->z2 = 0;
}

static void biquad_peak(biquad_t *q, float f0, float qf, float gain_db)
{
    float A = powf(10.0f, gain_db / 40.0f), w = 2.0f * (float)M_PI * f0 / s_rate;
    float c = cosf(w), al = sinf(w) / (2.0f * qf), a0 = 1 + al / A;
    q->b0 = (1 + al * A) / a0; q->b1 = -2 * c / a0; q->b2 = (1 - al * A) / a0;
    q->a1 = -2 * c / a0; q->a2 = (1 - al / A) / a0;
    q->z1 = q->z2 = 0;
}

static inline float biquad_run(biquad_t *q, float x)
{
    float y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2;
    q->z2 = q->b2 * x - q->a2 * y;
    return y;
}

static uint32_t s_rng = 0xC0FFEE;
static inline float noisef(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return ((int32_t)s_rng >> 8) / 8388608.0f;
}

typedef enum { VOX_IDLE, VOX_OPEN, VOX_KEYDOWN } vox_t;
static vox_t s_vox = VOX_IDLE;
static float s_env;
static int s_hang;
static int s_open_n, s_duck;        /* samples since the gate opened; key-up head to wait out */
#define VOX_HANG (s_rate * 35 / 100)            /* 350 ms */

/* SOFT CLIP: straight up to the knee, then a smooth curve that approaches full scale, so
 * loud peaks are rounded instead of cut off square. knee 1.0 = plain hard clip. */
static inline float soft_clip(float x)
{
    float a = x < 0 ? -x : x;
    if (a <= s_knee) return x;
    float y = s_knee >= 0.999f ? 1.0f : s_knee + (1.0f - s_knee) * tanhf((a - s_knee) / (1.0f - s_knee));
    if (y > 1.0f) y = 1.0f;
    return x < 0 ? -y : y;
}

static inline float radio(float x)
{
    float fx = P.fx / 100.0f;
    x = biquad_run(&s_hpf2, biquad_run(&s_hpf, x));      /* thin: almost nothing below 700 Hz */
    x = biquad_run(&s_pk2, biquad_run(&s_pk1, x));       /* bright: the helmet radio's presence */
    x = biquad_run(&s_lpf2, biquad_run(&s_lpf, x));
    x *= (1.0f + 7.0f * fx) * RADIO_MAKEUP;              /* drive 1x..8x */
    x = soft_clip(x);
    x = x - (x * x * x) / 3.0f;                 /* radio saturation curve */
    x = biquad_run(&s_round, x);                /* round the corners of clipped peaks */
    /* after the drive, like a real radio: the distortion re-creates lows, so cut them again,
     * and lift the presence band once more */
    x = biquad_run(&s_post_hp2, biquad_run(&s_post_hp, x));
    x = biquad_run(&s_post_pk, x) * POST_MAKEUP;
    float steps = 256.0f - 192.0f * fx;          /* bitcrush 8-bit .. ~6-bit */
    return (int32_t)(x * steps) / steps;
}

void voicefx_init(int rate_hz)
{
    s_rate = rate_hz;
    biquad_set(&s_hpf, 700.0f, true);
    biquad_set(&s_hpf2, 700.0f, true);
    biquad_set(&s_lpf, 3200.0f, false);
    biquad_set(&s_lpf2, 3200.0f, false);
    biquad_peak(&s_pk1, 1700.0f, 1.0f, 6.0f);
    biquad_peak(&s_pk2, 2500.0f, 1.4f, 4.0f);
    biquad_set(&s_post_hp, 750.0f, true);
    biquad_set(&s_post_hp2, 750.0f, true);
    biquad_peak(&s_post_pk, 2000.0f, 0.8f, 5.0f);
    s_vox = VOX_IDLE; s_env = 0; s_hang = 0;
    vshift_init(rate_hz);
    vshift_set(P.shift_on, P.pitch_ratio, P.formant_ratio);
    keysnd_init(rate_hz);
}

void voicefx_set(const voicefx_params_t *p)
{
    P = *p;
    s_gain = powf(10.0f, P.mic_gain / 20.0f);
    s_knee = 1.0f - 0.6f * P.soft_clip / 100.0f;                 /* 50 -> rounding starts at 70 % */
    float fr = 5000.0f - 25.0f * P.soft_clip;                     /* 0 -> 5 kHz, 100 -> 2.5 kHz */
    if (fr > s_rate * 0.45f) fr = s_rate * 0.45f;
    biquad_set(&s_round, fr, false);
    vshift_set(P.shift_on, P.pitch_ratio, P.formant_ratio);
    vshift_set_range(P.f0_ref, P.range);
}

bool voicefx_keyed(void) { return s_vox != VOX_IDLE || keysnd_active(); }

int16_t voicefx_process(float x_q15, bool *clipped)
{
    float x = soft_clip(x_q15 / 32768.0f * s_gain);   /* MIC GAIN, rounded instead of clipped */
    float v = vshift_process(x);                 /* shifted voice (32 ms late when on) */

    float a = fabsf(x);                          /* the gate listens to the raw mic */
    s_env += (a - s_env) * (a > s_env ? 0.05f : 0.002f);
    bool above = s_env * 100.0f > (float)P.vox;
    float kv = 0.9f * P.click / 100.0f;
    switch (s_vox) {
    case VOX_IDLE:
        if (above) {
            s_vox = VOX_OPEN; s_hang = VOX_HANG; keysnd_start(true, kv);
            s_open_n = 0; s_duck = keysnd_head();            /* the key-up comes first */
        }
        break;
    case VOX_OPEN:
        if (above) s_hang = VOX_HANG;
        else if (--s_hang <= 0) { s_vox = VOX_KEYDOWN; keysnd_start(false, kv); }
        break;
    case VOX_KEYDOWN:
        if (!keysnd_active()) s_vox = VOX_IDLE;
        else if (above) { s_vox = VOX_OPEN; s_hang = VOX_HANG; }   /* talked again: reopen */
        break;
    }

    float out = 0;
    if (s_vox == VOX_OPEN) {
        /* the voice comes in after the key-up's click (40 ms fade), like a real radio */
        float g = 1.0f;
        if (s_open_n < s_duck) g = 0.0f;
        else if (s_open_n < s_duck + s_rate / 25) g = (float)(s_open_n - s_duck) / (s_rate / 25);
        s_open_n++;
        out = radio(v) * g;
        out += noisef() * 0.12f * (P.stat / 100.0f);
    }
    out += keysnd_next();

    int32_t s = (int32_t)(out * 32767.0f);
    if (s > 32767) { s = 32767; *clipped = true; }
    if (s < -32768) { s = -32768; *clipped = true; }
    return (int16_t)s;
}
