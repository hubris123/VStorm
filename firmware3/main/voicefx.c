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
static voicefx_params_t P = {70, 22, 15, 60, false, 1.0f, 1.0f, 0.0f, 1.0f};

typedef struct { float b0, b1, b2, a1, a2, z1, z2; } biquad_t;
static biquad_t s_hpf, s_lpf;

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
#define VOX_HANG (s_rate * 35 / 100)            /* 350 ms */

static inline float radio(float x)
{
    float fx = P.fx / 100.0f;
    x = biquad_run(&s_hpf, x);
    x = biquad_run(&s_lpf, x);
    x *= 1.0f + 7.0f * fx;                      /* soft-clip drive 1x..8x */
    if (x > 1.0f) x = 1.0f; else if (x < -1.0f) x = -1.0f;
    x = x - (x * x * x) / 3.0f;
    float steps = 256.0f - 192.0f * fx;          /* bitcrush 8-bit .. ~6-bit */
    return (int32_t)(x * steps) / steps;
}

void voicefx_init(int rate_hz)
{
    s_rate = rate_hz;
    biquad_set(&s_hpf, 300.0f, true);
    biquad_set(&s_lpf, 3000.0f, false);
    s_vox = VOX_IDLE; s_env = 0; s_hang = 0;
    vshift_init(rate_hz);
    vshift_set(P.shift_on, P.pitch_ratio, P.formant_ratio);
    keysnd_init(rate_hz);
}

void voicefx_set(const voicefx_params_t *p)
{
    P = *p;
    vshift_set(P.shift_on, P.pitch_ratio, P.formant_ratio);
    vshift_set_range(P.f0_ref, P.range);
}

bool voicefx_keyed(void) { return s_vox != VOX_IDLE || keysnd_active(); }

int16_t voicefx_process(float x_q15, bool *clipped)
{
    float x = x_q15 / 32768.0f;
    float v = vshift_process(x);                 /* shifted voice (32 ms late when on) */

    float a = fabsf(x);                          /* the gate listens to the raw mic */
    s_env += (a - s_env) * (a > s_env ? 0.05f : 0.002f);
    bool above = s_env * 100.0f > (float)P.vox;
    float kv = 0.9f * P.click / 100.0f;
    switch (s_vox) {
    case VOX_IDLE:
        if (above) { s_vox = VOX_OPEN; s_hang = VOX_HANG; keysnd_start(true, kv); }
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
        out = radio(v);
        out += noisef() * 0.12f * (P.stat / 100.0f);
    }
    out += keysnd_next();

    int32_t s = (int32_t)(out * 32767.0f);
    if (s > 32767) { s = 32767; *clipped = true; }
    if (s < -32768) { s = -32768; *clipped = true; }
    return (int16_t)s;
}
