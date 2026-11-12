/* Radio key-up / key-down sounds, synthesised (no recorded audio on the device).
 *
 * Modelled on 18 reference radio sounds (tools/voice_analysis/keys_report.md). They are not
 * one sound with different settings but several kinds, so each press picks a kind (never
 * the same kind twice running) and then varies it:
 *   TOCK      one click + short ring + a little hiss
 *   STUTTER   2-4 quick clicks / pops in a row, uneven
 *   CARRIER   a flat block of radio hiss (90-160 ms), soft edges, no sharp click
 *   SQUELCH   hiss tail -> gap -> click (classic key-down)
 *   CHIRP     bright crack -> a brighter, slower-fading burst of static (no whistle)
 *   POPS      1-2 small pops -> a stronger click with a longer static tail
 * Each burst is broad noise centred on one of the reference bands: low ~340 / 800-1050,
 * mid 1380-1700, high 2150-2650 Hz. Never a tuned ring: the references have no tones
 * (tonality checked: tools/voice_analysis). Some sounds carry a dim background hiss, some are clean.
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

static void bq_band(bq_t *q, float f0, float qf, int rate)
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
static inline float jit(float lo, float hi) { return lo + (hi - lo) * rnd01(); }

enum { TOCK, STUTTER, CARRIER, SQUELCH, CHIRP, POPS, NKINDS };

#define MAX_CLICKS 5
#define MAX_BLOCKS 2

typedef struct {               /* a click: a sharp impulse + a very short burst of coloured noise */
    int at;
    float amp, bright;         /* bright: how much raw crack gets past the radio band */
    bq_t colour;               /* broad band-pass (Q ~1): colours the burst, never whistles */
    float decay, env;          /* per-sample decay of the burst */
} click_t;

typedef struct {               /* a block of band-limited radio hiss with soft or hard edges */
    int start, end, edge;
    float amp, wobble;
    bq_t band;
} block_t;

static int s_rate = 16000;
static struct {
    int n, len, kind;
    click_t c[MAX_CLICKS];
    int nc;
    block_t b[MAX_BLOCKS];
    int nb;
    float floor_amp, vol;
    bq_t floor_band, lp1, lp2, hp, crack_hp;
} K;

void keysnd_init(int rate_hz) { s_rate = rate_hz; K.n = K.len = 0; }
bool keysnd_active(void) { return K.n < K.len; }
int keysnd_kind(void) { return K.kind; }

static float ring_pitch(void)
{
    float g = rnd01();
    if (g < 0.36f) return rnd01() < 0.15f ? jit(320, 360) : jit(800, 1050);   /* low  */
    if (g < 0.66f) return jit(1380, 1700);                                     /* mid  */
    return jit(2150, 2650);                                                    /* high */
}

static void add_click(int at, float amp, float f, float t20, float bright)
{
    if (K.nc >= MAX_CLICKS) return;
    click_t *c = &K.c[K.nc++];
    c->at = at;
    c->amp = amp;
    c->bright = bright;
    if (f > s_rate * 0.42f) f = s_rate * 0.42f;
    float tau = t20 / 2.303f;                     /* burst falls 20 dB in t20 seconds */
    c->decay = expf(-1.0f / (tau * s_rate));
    c->env = 1.0f;
    bq_band(&c->colour, f, jit(0.7f, 1.4f), s_rate);
}

static void add_block(int start, int end, float f, float qf, float amp, int edge)
{
    if (K.nb >= MAX_BLOCKS) return;
    block_t *b = &K.b[K.nb++];
    b->start = start; b->end = end; b->edge = edge > 1 ? edge : 1;
    b->amp = amp; b->wobble = rnd01();
    bq_band(&b->band, f, qf, s_rate);
}

#define MS(x) ((int)((x) * s_rate / 1000.0f))

void keysnd_start(bool keyup, float volume)
{
    static int last = -1;
    /* key-up leans to sharp starts, key-down to tails and blocks */
    static const float W_UP[NKINDS]   = {3, 3, 1, 1, 3, 2};
    static const float W_DOWN[NKINDS] = {2, 2, 3, 3, 1, 1};
    const float *w = keyup ? W_UP : W_DOWN;
    int kind;
    do {
        float tot = 0, r;
        for (int i = 0; i < NKINDS; i++) tot += w[i];
        r = rnd01() * tot;
        for (kind = 0; kind < NKINDS - 1 && r >= w[kind]; kind++) r -= w[kind];
    } while (kind == last);
    last = kind;

    K.kind = kind; K.nc = K.nb = 0; K.vol = volume;
    float f = ring_pitch();
    int end = 0;
    switch (kind) {
    case TOCK: {
        int at = MS(jit(15, 60));
        add_click(at, jit(0.8f, 1.0f), f, jit(0.010f, 0.03f), jit(0.0f, 0.3f));
        add_block(at, at + MS(jit(30, 70)), jit(1000, 1800), 0.7f, jit(0.02f, 0.07f), MS(25));
        end = at + MS(jit(60, 110));
        break;
    }
    case STUTTER: {
        int n = 2 + (int)(rnd01() * 3), at = MS(jit(10, 40));
        for (int i = 0; i < n; i++) {
            add_click(at, jit(0.35f, 1.0f), rnd01() < 0.6f ? f : ring_pitch(), jit(0.008f, 0.025f), jit(0.0f, 0.4f));
            at += MS(jit(14, 55));
        }
        add_block(MS(5), at, jit(1000, 1800), 0.6f, jit(0.015f, 0.05f), MS(8));
        end = at + MS(jit(40, 70));
        break;
    }
    case CARRIER: {
        int s = MS(jit(10, 40)), e = s + MS(jit(90, 160));
        add_block(s, e, jit(1200, 2200), jit(0.6f, 1.0f), jit(0.25f, 0.42f), MS(jit(6, 18)));
        if (rnd01() < 0.4f) add_click(s, jit(0.3f, 0.6f), f, 0.015f, 0.1f);
        end = e + MS(jit(10, 30));
        break;
    }
    case SQUELCH: {
        int e = MS(jit(45, 110)), at = e + MS(jit(10, 35));
        add_block(0, e, jit(1100, 2200), jit(0.6f, 1.2f), jit(0.13f, 0.26f), MS(3));
        add_click(at, jit(0.8f, 1.0f), f, jit(0.010f, 0.03f), jit(0.1f, 0.4f));
        end = at + MS(jit(50, 90));
        break;
    }
    case CHIRP: {
        int at = MS(jit(15, 50));
        float cf = jit(2050, 2600);
        add_click(at, 1.0f, cf, jit(0.05f, 0.09f), jit(0.5f, 0.9f));     /* long whistle */
        add_block(at, at + MS(jit(60, 110)), cf, 0.9f, jit(0.12f, 0.25f), MS(40));
        end = at + MS(jit(110, 160));
        break;
    }
    default: {                                                            /* POPS */
        int at = MS(jit(10, 30)), np = 1 + (int)(rnd01() * 2);
        for (int i = 0; i < np; i++) {
            add_click(at, jit(0.2f, 0.4f), ring_pitch(), 0.012f, 0.1f);
            at += MS(jit(25, 45));
        }
        add_click(at, jit(0.8f, 1.0f), f, jit(0.03f, 0.06f), jit(0.1f, 0.3f));
        add_block(at, at + MS(jit(50, 90)), f, 0.8f, jit(0.04f, 0.09f), MS(30));
        end = at + MS(jit(90, 130));
        break;
    }
    }
    K.len = end;
    /* some sounds carry a dim full-band hiss under everything (like an open channel) */
    K.floor_amp = rnd01() < 0.8f ? jit(0.008f, 0.02f) : 0.0f;
    bq_band(&K.floor_band, jit(1500, 3000), 0.4f, s_rate);
    bq_pass(&K.lp1, jit(1900, 3000), false, s_rate);       /* the radio's band limit */
    bq_pass(&K.lp2, 3400, false, s_rate);
    bq_pass(&K.hp, jit(380, 520), true, s_rate);
    bq_pass(&K.crack_hp, 3000, true, s_rate);
    K.n = 0;
}

float keysnd_next(void)
{
    if (K.n >= K.len) return 0.0f;
    int n = K.n++;
    float body = 0, crack = 0;
    for (int i = 0; i < K.nc; i++) {
        click_t *c = &K.c[i];
        int since = n - c->at;
        if (since < 0) continue;
        float imp = since == 0 ? c->amp : since == 1 ? -0.6f * c->amp : 0.0f;
        float burst = noise() * c->env * c->amp;
        c->env *= c->decay;
        body += bq_run(&c->colour, burst) * 2.2f + imp * 0.5f;
        crack += imp * c->bright;
    }
    for (int i = 0; i < K.nb; i++) {
        block_t *b = &K.b[i];
        if (n < b->start || n >= b->end) continue;
        float e = 1.0f;
        if (n - b->start < b->edge) e = (float)(n - b->start) / b->edge;
        if (b->end - n < b->edge) e = (float)(b->end - n) / b->edge;
        float wob = 1.0f + 0.25f * sinf(n * 0.0021f * (1.0f + b->wobble * 3.0f));   /* carrier flutter */
        body += bq_run(&b->band, noise()) * b->amp * e * wob * (K.kind == CARRIER ? 3.0f : K.kind == SQUELCH ? 1.6f : 0.45f);
    }
    body += bq_run(&K.floor_band, noise()) * K.floor_amp * 3.0f;
    float out = bq_run(&K.hp, bq_run(&K.lp2, bq_run(&K.lp1, body))) * 1.6f
              + bq_run(&K.crack_hp, crack) * 0.5f;                    /* bright cracks reach past the band */
    if (out > 1.0f) out = 1.0f;
    if (out < -1.0f) out = -1.0f;
    return out * K.vol;
}
