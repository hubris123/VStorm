/* Voice shift: learned pitch + tone (formant) shift onto a target voice. See vshift.h.
 *
 * Pipeline per sample (16 kHz):
 *   input ring  -> pitch tracker (every 8 ms: normalised autocorrelation on a 900 Hz
 *                  low-passed, 2x decimated copy; 80-400 Hz)
 *               -> analysis marks one pitch period apart, snapped to the waveform's peaks
 *               -> PSOLA: for each output mark (spaced period / pitch_ratio) take the nearest
 *                  input grain (two periods, Hann window), resample it by formant_ratio
 *                  (stretching a grain moves its formants), overlap-add into the output
 *   unvoiced sounds (s, f, t, breath) use a fixed 10 ms grain with no pitch change, so they
 *   pass through clean but still get the tone shift.
 * Fixed latency L = 512 samples (32 ms) while on; straight through when off.
 * Learning (while running): pitch histogram + LPC formant histograms -> medians -> ratios. */
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "vshift.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const vs_target_t VS_TROOPER = {"TROOPER", 125.0f, 1496.0f, 2335.0f};

#define N        4096                 /* ring sizes (power of two) */
#define M        (N - 1)
#define LAT      512                  /* output latency, samples */
#define HALFMAX  300                  /* longest half grain on the output side */
#define HOP      128                  /* pitch tracker period (8 ms) */
#define WIN      512                  /* pitch tracker window (32 ms) */
#define UNV_T    160                  /* unvoiced pseudo-period (10 ms) */

static int s_rate = 16000;
static float IN[N], LPB[N], OUT[N];
static uint32_t ni;                   /* input samples written */
static bool s_on;
static float s_pr = 1.0f, s_fr = 1.0f;            /* pitch ratio, formant ratio */
static float s_ref = 0.0f, s_range = 1.0f;        /* pitch-swing narrowing around the learned pitch */
static float s_T = UNV_T;                         /* current period (samples) */
static bool s_voiced;
static float s_live_f0;
static double sm;                                 /* next synthesis mark (absolute) */
static int64_t am_prev, am_next;                  /* analysis marks around sm */
static bool marks_ok;

/* 900 Hz low-pass (biquad) for the tracker and mark snapping */
static float lb0, lb1, lb2, la1, la2, lz1, lz2;
static void lp_init(void)
{
    float w = 2.0f * (float)M_PI * 900.0f / s_rate, c = cosf(w), s = sinf(w), al = s / (2.0f * 0.707f), a0 = 1 + al;
    lb0 = (1 - c) / 2 / a0; lb1 = (1 - c) / a0; lb2 = lb0; la1 = -2 * c / a0; la2 = (1 - al) / a0;
    lz1 = lz2 = 0;
}
static inline float lp_run(float x)
{
    float y = lb0 * x + lz1;
    lz1 = lb1 * x - la1 * y + lz2;
    lz2 = lb2 * x - la2 * y;
    return y;
}

/* ---------------- learning state ---------------- */
#define F0_BINS 160                    /* quarter-semitone bins from 50 Hz */
#define FM_BINS 160                    /* 25 Hz bins up to 4 kHz */
static bool s_learn;
static uint16_t h_f0[F0_BINS], h_f1[FM_BINS], h_f2[FM_BINS], h_f3[FM_BINS];
static uint32_t n_voiced, n_fm;
static int fm_skip;

void vlearn_start(void)
{
    memset(h_f0, 0, sizeof h_f0); memset(h_f1, 0, sizeof h_f1);
    memset(h_f2, 0, sizeof h_f2); memset(h_f3, 0, sizeof h_f3);
    n_voiced = n_fm = 0;
    fm_skip = 0;
    s_learn = true;
}
void vlearn_stop(void) { s_learn = false; }
void vlearn_resume(void) { s_learn = true; }
bool vlearn_running(void) { return s_learn; }

static void hist_add(uint16_t *h, int nb, int b) { if (b >= 0 && b < nb && h[b] < 65535) h[b]++; }

static float hist_median(const uint16_t *h, int nb, uint32_t *count)
{
    uint32_t tot = 0;
    for (int i = 0; i < nb; i++) tot += h[i];
    *count = tot;
    if (!tot) return -1;
    float half = tot * 0.5f, acc = 0;
    for (int i = 0; i < nb; i++) {                               /* interpolated inside the bin */
        if (h[i] && acc + h[i] >= half) return i + (half - acc) / h[i];
        acc += h[i];
    }
    return nb - 0.5f;
}

/* LPC (order 14) formants of the last 400 input samples: envelope peaks in the usual ranges */
#define LPC_P 14
static void learn_formants(void)
{
    float x[400], r[LPC_P + 1], a[LPC_P + 1], tmp[LPC_P + 1];
    uint32_t base = ni - 400;
    float prev = IN[(base - 1) & M];
    for (int i = 0; i < 400; i++) {
        float v = IN[(base + i) & M];
        x[i] = (v - 0.63f * prev) * (0.54f - 0.46f * cosf(2.0f * (float)M_PI * i / 399.0f));
        prev = v;
    }
    for (int k = 0; k <= LPC_P; k++) {
        float s = 0;
        for (int i = k; i < 400; i++) s += x[i] * x[i - k];
        r[k] = s;
    }
    if (r[0] <= 1e-9f) return;
    memset(a, 0, sizeof a);
    a[0] = 1;
    float e = r[0];
    for (int i = 1; i <= LPC_P; i++) {                          /* Levinson-Durbin */
        float acc = r[i];
        for (int j = 1; j < i; j++) acc += a[j] * r[i - j];
        float k = -acc / e;
        memcpy(tmp, a, sizeof a);
        for (int j = 1; j < i; j++) a[j] = tmp[j] + k * tmp[i - j];
        a[i] = k;
        e *= 1 - k * k;
        if (e <= 0) return;
    }
    /* envelope 1/|A| at 25 Hz steps to 4 kHz; peaks = formant candidates */
    float env[FM_BINS];
    for (int b = 0; b < FM_BINS; b++) {
        float w = 2.0f * (float)M_PI * (b * 25.0f + 12.5f) / s_rate;
        float cr = cosf(w), ci = -sinf(w), pr = 1, pi = 0, re = 0, im = 0;   /* z^-k by rotation */
        for (int k = 0; k <= LPC_P; k++) {
            re += a[k] * pr; im += a[k] * pi;
            float t = pr * cr - pi * ci; pi = pr * ci + pi * cr; pr = t;
        }
        env[b] = 1.0f / (re * re + im * im + 1e-12f);
    }
    int pk[8], np = 0;
    for (int b = 1; b < FM_BINS - 1 && np < 8; b++)
        if (env[b] > env[b - 1] && env[b] >= env[b + 1]) pk[np++] = b;
    /* F1 200-1000, F2 700-2700 (> F1 + 200), F3 1700-3900 (> F2 + 300) */
    float f1 = 0, f2 = 0, f3 = 0;
    for (int i = 0; i < np; i++) {
        float f = pk[i] * 25.0f + 12.5f;
        if (!f1 && f >= 200 && f <= 1000) f1 = f;
        else if (f1 && !f2 && f >= 700 && f <= 2700 && f > f1 + 200) f2 = f;
        else if (f2 && !f3 && f >= 1700 && f <= 3900 && f > f2 + 300) f3 = f;
    }
    if (f1) hist_add(h_f1, FM_BINS, (int)(f1 / 25));
    if (f2) hist_add(h_f2, FM_BINS, (int)(f2 / 25));
    if (f3) hist_add(h_f3, FM_BINS, (int)(f3 / 25));
    n_fm++;
}

bool vlearn_result(const vs_target_t *t, vs_learn_t *o)
{
    uint32_t c0, c1, c2, c3;
    float b0 = hist_median(h_f0, F0_BINS, &c0);
    float b1 = hist_median(h_f1, FM_BINS, &c1), b2 = hist_median(h_f2, FM_BINS, &c2), b3 = hist_median(h_f3, FM_BINS, &c3);
    memset(o, 0, sizeof *o);
    o->seconds = n_voiced * (float)HOP / s_rate;
    if (c0 < 60) return false;                                  /* need ~0.5 s of voiced speech */
    o->f0 = 50.0f * powf(2.0f, b0 / 48.0f);
    o->f1 = c1 >= 10 ? b1 * 25.0f : 0;
    o->f2 = c2 >= 10 ? b2 * 25.0f : 0;
    o->f3 = c3 >= 10 ? b3 * 25.0f : 0;
    float pr = t->f0 / o->f0;
    float fr;
    /* F3 tracks vocal-tract length best and barely moves with the vowel; F2 swings with
     * every vowel, so it is only the fallback */
    if (o->f3) fr = t->f3 / o->f3;
    else if (o->f2) fr = t->f2 / o->f2;
    else fr = powf(pr, 0.27f);                                  /* no formants: follow pitch, gently */
    /* sanity: a voice's throat size goes with its pitch (about pitch_ratio^0.27); keep the
     * tone within 10 % of that, so a bad formant reading (weak F3, noise) cannot run away */
    float expect = powf(pr, 0.27f);
    if (fr < expect * 0.9f) fr = expect * 0.9f;
    if (fr > expect * 1.1f) fr = expect * 1.1f;
    o->pitch_ratio = pr < 0.45f ? 0.45f : pr > 1.35f ? 1.35f : pr;
    o->formant_ratio = fr < 0.72f ? 0.72f : fr > 1.12f ? 1.12f : fr;
    return true;
}

/* ---------------- pitch tracker ---------------- */
static void pitch_update(void)
{
    /* 2x decimated low-band window: 256 samples at 8 kHz, lags 20..100 (80-400 Hz) */
    float d[WIN / 2];
    float e = 0;
    for (int i = 0; i < WIN / 2; i++) {
        float v = LPB[(ni - WIN + 2 * i) & M];
        d[i] = v;
        e += v * v;
    }
    float rms_in = 0;
    for (int i = 0; i < WIN; i += 4) { float v = IN[(ni - WIN + i) & M]; rms_in += v * v; }
    rms_in = sqrtf(rms_in / (WIN / 4));
    const int LMIN = 20, LMAX = 100, NN = WIN / 2 - LMAX;
    float r[LMAX + 1], best = 0;
    int bl = 0;
    float e0 = 0;
    for (int i = 0; i < NN; i++) e0 += d[i] * d[i];
    for (int l = LMIN; l <= LMAX; l++) {
        float s = 0, el = 0;
        for (int i = 0; i < NN; i++) { s += d[i] * d[i + l]; el += d[i + l] * d[i + l]; }
        r[l] = s / sqrtf(e0 * el + 1e-12f);
        if (r[l] > best) { best = r[l]; bl = l; }
    }
    bool voiced = rms_in > 0.004f && best > 0.55f;
    if (voiced) {
        for (int l = LMIN + 1; l < LMAX; l++)                    /* earliest strong peak */
            if (r[l] > 0.88f * best && r[l] >= r[l - 1] && r[l] >= r[l + 1]) { bl = l; break; }
        float lag = (float)bl;
        if (bl > LMIN && bl < LMAX) {
            float a = r[bl - 1], b = r[bl], c = r[bl + 1], den = a - 2 * b + c;
            if (den) lag += 0.5f * (a - c) / den;
        }
        float T = 2.0f * lag;
        if (s_voiced && fabsf(logf(T / s_T)) < 0.3f) s_T = 0.6f * s_T + 0.4f * T;   /* glide */
        else s_T = T;
        s_live_f0 = s_rate / s_T;
        if (s_learn) {
            int b = (int)floorf(48.0f * log2f(s_live_f0 / 50.0f));
            hist_add(h_f0, F0_BINS, b);
            n_voiced++;
            if (++fm_skip >= 4) { fm_skip = 0; learn_formants(); }
        }
    } else {
        s_live_f0 = 0;
    }
    s_voiced = voiced;
}

/* ---------------- PSOLA ---------------- */
static int64_t next_mark(int64_t prev, float T)
{
    int64_t p = prev + (int64_t)(T + 0.5f);
    if (!s_voiced) return p;
    int w = (int)(T * 0.25f);                                   /* snap to the local peak */
    int64_t best = p;
    float bv = -1e9f;
    for (int64_t k = p - w; k <= p + w; k++) {
        if (k >= (int64_t)ni - 1) break;
        float v = LPB[k & M];
        if (v > bv) { bv = v; best = k; }
    }
    return best > prev + 8 ? best : p;
}

static inline float in_at(double pos)
{
    int64_t i = (int64_t)floor(pos);
    float f = (float)(pos - i);
    return IN[i & M] * (1 - f) + IN[(i + 1) & M] * f;
}

static void place_grain(void)
{
    float T = s_voiced ? s_T : UNV_T;
    if (!marks_ok) {
        am_prev = (int64_t)sm;
        am_next = next_mark(am_prev, T);
        marks_ok = true;
    }
    while (am_next <= (int64_t)sm) { am_prev = am_next; am_next = next_mark(am_prev, T); }
    int64_t a = (sm - am_prev < am_next - sm) ? am_prev : am_next;
    float half_out = T / s_fr;
    if (half_out > HALFMAX - 4) half_out = HALFMAX - 4;
    int h = (int)half_out;
    int64_t c = (int64_t)floor(sm + 0.5);
    /* keep loudness: overlapping Hann grains sum to (pitch/formant) for voiced, 1/formant unvoiced */
    float g = s_voiced ? s_fr / s_pr : s_fr;
    if (g > 1.6f) g = 1.6f;
    for (int j = -h; j <= h; j++) {
        float w = g * (0.5f + 0.5f * cosf((float)M_PI * j / (half_out + 1)));
        OUT[(c + j) & M] += w * in_at((double)a + j * s_fr);
    }
    float pr = s_pr;
    if (s_voiced && s_ref > 0 && s_range < 0.999f) {              /* pull swings toward the centre */
        float k = powf((s_rate / T) / s_ref, s_range - 1.0f);
        if (k < 0.7f) k = 0.7f;
        if (k > 1.4f) k = 1.4f;
        pr *= k;
    }
    sm += s_voiced ? T / pr : T;
}

/* ---------------- public ---------------- */
void vshift_init(int rate_hz)
{
    s_rate = rate_hz;
    memset(IN, 0, sizeof IN); memset(LPB, 0, sizeof LPB); memset(OUT, 0, sizeof OUT);
    ni = 0;
    s_T = UNV_T; s_voiced = false; s_live_f0 = 0;
    sm = 0; marks_ok = false;
    lp_init();
}

void vshift_set(bool on, float pitch_ratio, float formant_ratio)
{
    if (on && !s_on) {                                          /* (re)start the grain clock */
        memset(OUT, 0, sizeof OUT);
        sm = (double)ni - LAT + HALFMAX;
        marks_ok = false;
    }
    s_on = on;
    s_pr = pitch_ratio < 0.4f ? 0.4f : pitch_ratio > 1.5f ? 1.5f : pitch_ratio;
    s_fr = formant_ratio < 0.7f ? 0.7f : formant_ratio > 1.15f ? 1.15f : formant_ratio;
}

float vshift_live_f0(void) { return s_live_f0; }

void vshift_set_range(float f0_ref, float range)
{
    s_ref = f0_ref;
    s_range = range < 0.3f ? 0.3f : range > 1.0f ? 1.0f : range;
}

float vshift_process(float x)
{
    IN[ni & M] = x;
    LPB[ni & M] = lp_run(x);
    ni++;
    if ((ni & (HOP - 1)) == 0 && ni >= WIN) pitch_update();
    if (!s_on) return x;
    while (sm <= (double)ni - LAT + HALFMAX) place_grain();
    if (ni < LAT) return 0;
    uint32_t o = ni - LAT;
    float y = OUT[o & M];
    OUT[o & M] = 0;
    return y;
}

float vs_ratio_to_semitones(float r) { return 12.0f * log2f(r); }
float vs_semitones_to_ratio(float st) { return powf(2.0f, st / 12.0f); }
