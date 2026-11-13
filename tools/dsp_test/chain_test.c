/* Whole voice chain test (firmware3/main/voicefx.c): learn a voice, then render it through
 * the device chain twice - radio effect only, then with the learned TROOPER shift - into one
 * WAV (before, 1 s gap, after).
 *   gcc -O2 -I firmware3/main -I tools/dsp_test tools/dsp_test/chain_test.c \
 *       firmware3/main/voicefx.c firmware3/main/vshift.c firmware3/main/keysnd.c -lm -o ct
 *   ./ct in.wav out.wav */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "voicefx.h"

#define NO_MAIN
#include "render_keys.c"

static float *read_wav(const char *path, int *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(sz);
    if (fread(b, 1, sz, f) != (size_t)sz) { fclose(f); return NULL; }
    fclose(f);
    long p = 12;
    while (p + 8 < sz && memcmp(b + p, "data", 4)) p += 8 + *(uint32_t *)(b + p + 4);
    *n = *(uint32_t *)(b + p + 4) / 2;
    float *x = malloc(*n * sizeof(float));
    for (int i = 0; i < *n; i++) x[i] = ((int16_t *)(b + p + 8))[i] / 32768.0f;
    free(b);
    return x;
}

int main(int argc, char **argv)
{
    if (argc < 3) return 1;
    int n;
    float *x = read_wav(argv[1], &n);
    if (!x) return 1;
    float *y = calloc(2 * n + 16000, sizeof(float));
    int gain = argc > 3 ? atoi(argv[3]) : 0;                 /* MIC GAIN dB */
    voicefx_params_t p = {70, 22, 15, 60, false, 1.0f, 1.0f, 0.0f, 1.0f, gain, 50};
    if (argc > 4) voicefx_tailcut(atoi(argv[4]) != 0);
    bool c;
    /* learn while listening (as on the VOICE page) */
    voicefx_init(16000); voicefx_set(&p);
    vlearn_start();
    for (int i = 0; i < n; i++) voicefx_process(x[i] * 32767.0f, &c);
    vlearn_stop();
    vs_learn_t r;
    vlearn_result(&VS_TROOPER, &r);
    printf("learned f0 %.0f Hz, F3 %.0f, swing %.1f st -> pitch %+.1f st, tone %+.0f %%, swing x%.2f\n", r.f0, r.f3,
           r.swing, vs_ratio_to_semitones(r.pitch_ratio), (r.formant_ratio - 1) * 100, r.range);
    /* before: radio effect only */
    voicefx_init(16000); voicefx_set(&p);
    for (int i = 0; i < n; i++) y[i] = voicefx_process(x[i] * 32767.0f, &c) / 32768.0f;
    /* after: with the learned shift */
    p.shift_on = true; p.pitch_ratio = r.pitch_ratio; p.formant_ratio = r.formant_ratio;
    p.f0_ref = r.f0; p.range = r.range;
    voicefx_init(16000); voicefx_set(&p);
    for (int i = 0; i < n; i++) y[n + 16000 + i] = voicefx_process(x[i] * 32767.0f, &c) / 32768.0f;
    wav_write(argv[2], y, 2 * n + 16000, 16000);
    return 0;
}
