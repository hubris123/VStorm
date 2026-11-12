/* Stormtrooper voice DSP — hardware-free, shared by the firmware (audio.c)
 * and the PC/browser simulator (sim/). Moved verbatim from audio.c; the only
 * change is that the sample rate is a runtime value instead of a macro. */
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include "settings.h"
#include "dsp.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int s_rate = 16000;

/* ---------------- stormtrooper DSP ----------------
 * band-pass 300 Hz..3 kHz -> soft-clip drive -> bitcrush -> static bed,
 * gated by VOX with key-up/key-down click bursts. Floats: S3 has FPU,
 * trivial load at 16 kHz. */

typedef struct { float b0, b1, b2, a1, a2, z1, z2; } biquad_t;
static biquad_t s_hpf, s_lpf;

static void biquad_set(biquad_t *q, float f0, bool highpass)
{
    float w = 2.0f * (float)M_PI * f0 / s_rate;
    float c = cosf(w), s = sinf(w), alpha = s / (2.0f * 0.707f);
    float a0 = 1 + alpha;
    if (highpass) {
        q->b0 = (1 + c) / 2 / a0; q->b1 = -(1 + c) / a0; q->b2 = q->b0;
    } else {
        q->b0 = (1 - c) / 2 / a0; q->b1 = (1 - c) / a0; q->b2 = q->b0;
    }
    q->a1 = (-2 * c) / a0; q->a2 = (1 - alpha) / a0;
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
static inline float noisef(void) /* white noise -1..1 */
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return ((int32_t)s_rng >> 8) / 8388608.0f;
}

/* VOX state */
typedef enum { VOX_IDLE, VOX_KEYUP, VOX_OPEN, VOX_KEYDOWN } vox_state_t;
static vox_state_t s_vox = VOX_IDLE;
static float s_env = 0;
static int s_hang = 0;
static int s_click_n = 0;       /* samples remaining in click burst */
static float s_click_amp = 0;
static int s_click_variant = 0; /* placeholder pool: vary decay/len; real WAVs drop in for M2 final */
#define VOX_HANG_SAMPLES (s_rate * 35 / 100)  /* 350 ms hang */

static void start_click(bool keyup)
{
    static int last = -1;
    int v;
    do { v = (int)(noisef() * 100.0f) & 3; } while (v == last);
    last = v;
    s_click_variant = v;
    s_click_n = s_rate * (keyup ? 30 : 50 + 10 * v) / 1000; /* 30-90 ms */
    s_click_amp = 0.9f * g_settings.click_vol / 100.0f;
}

/* per-sample effect; in/out Q15 as float */
static inline float fx_process(float x)
{
    float fx = g_settings.fx_intensity / 100.0f;

    /* band-pass */
    x = biquad_run(&s_hpf, x);
    x = biquad_run(&s_lpf, x);

    /* soft-clip drive: 1x..8x into tanh-ish curve */
    float drive = 1.0f + 7.0f * fx;
    x *= drive;
    if (x > 1.0f) x = 1.0f; else if (x < -1.0f) x = -1.0f;
    x = x - (x * x * x) / 3.0f;

    /* subtle bitcrush, deepens with intensity */
    float steps = 256.0f - 192.0f * fx;   /* 8-bit-ish down to ~6-bit */
    x = (int32_t)(x * steps) / steps;

    return x;
}

int16_t dsp_process(float x_q15, bool *clipped)
{
    /* envelope, normalised to 0..1 so it compares against vox_thresh (0..100).
     * (Was un-normalised Q15: ADC noise alone held the gate open forever.) */
    float a = fabsf(x_q15) / 32768.0f;
    s_env += (a - s_env) * (a > s_env ? 0.05f : 0.002f);
    bool above = (s_env * 100.0f) > (float)g_settings.vox_thresh;

    switch (s_vox) {
    case VOX_IDLE:
        if (above) { s_vox = VOX_KEYUP; start_click(true); }
        break;
    case VOX_KEYUP:
        if (s_click_n <= 0) s_vox = VOX_OPEN;
        break;
    case VOX_OPEN:
        if (above) s_hang = VOX_HANG_SAMPLES;
        else if (--s_hang <= 0) { s_vox = VOX_KEYDOWN; start_click(false); }
        break;
    case VOX_KEYDOWN:
        if (s_click_n <= 0) s_vox = VOX_IDLE;
        break;
    }

    float out = 0;
    if (s_vox == VOX_OPEN) {
        out = fx_process(x_q15 / 32768.0f);
        /* static bed while keyed */
        out += noisef() * 0.12f * (g_settings.static_lvl / 100.0f);
    } else if (s_click_n > 0) {
        /* click burst: decaying band-passed noise pop */
        s_click_n--;
        float env = (float)s_click_n / (s_rate * 0.05f);
        if (env > 1.0f) env = 1.0f;
        out = biquad_run(&s_lpf, noisef()) * s_click_amp * env * (1.2f + 0.3f * s_click_variant);
    }


    int32_t s = (int32_t)(out * 32767.0f);
    if (s > 32767) { s = 32767; *clipped = true; }
    if (s < -32768) { s = -32768; *clipped = true; }
    return (int16_t)s;
}

void dsp_init(int sample_rate_hz)
{
    s_rate = sample_rate_hz;
    biquad_set(&s_hpf, 300.0f, true);
    biquad_set(&s_lpf, 3000.0f, false);
    s_vox = VOX_IDLE;
    s_env = 0;
    s_hang = 0;
    s_click_n = 0;
}

bool dsp_is_keyed(void) { return s_vox != VOX_IDLE; }
