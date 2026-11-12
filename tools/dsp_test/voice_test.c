/* Voice shift test: learn a voice from a WAV, then shift it onto the TROOPER target.
 *   gcc -O2 -I firmware3/main tools/dsp_test/voice_test.c firmware3/main/vshift.c -lm -o vt
 *   ./vt in.wav out.wav            (16 kHz mono 16-bit in; prints the learned numbers + cost) */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vshift.h"

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
    uint32_t len = *(uint32_t *)(b + p + 4);
    *n = len / 2;
    float *x = malloc(*n * sizeof(float));
    for (int i = 0; i < *n; i++) x[i] = ((int16_t *)(b + p + 8))[i] / 32768.0f;
    free(b);
    return x;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: vt in.wav out.wav [pitch_ratio formant_ratio]\n"); return 1; }
    int n;
    float *x = read_wav(argv[1], &n);
    if (!x) return 1;
    float *y = malloc(n * sizeof(float));
    /* pass 1: learn (shift off, as during the first seconds of reading) */
    vshift_init(16000);
    vlearn_start();
    for (int i = 0; i < n; i++) vshift_process(x[i]);
    vlearn_stop();
    vs_learn_t r;
    bool ok = vlearn_result(&VS_TROOPER, &r);
    float pr = r.pitch_ratio, fr = r.formant_ratio;
    if (argc >= 5) { pr = atof(argv[3]); fr = atof(argv[4]); }
    printf("learned: %s  f0 %.1f Hz  F1 %.0f  F2 %.0f  F3 %.0f  (%.1f s voiced)\n",
           ok ? "ok" : "NOT ENOUGH", r.f0, r.f1, r.f2, r.f3, r.seconds);
    printf("shift:   pitch x%.3f (%+.1f st)  tone x%.3f (%+.0f %%)\n", pr, vs_ratio_to_semitones(pr), fr, (fr - 1) * 100);
    /* pass 2: shift */
    vshift_init(16000);
    vshift_set(true, pr, fr);
    clock_t t0 = clock();
    for (int i = 0; i < n; i++) y[i] = vshift_process(x[i]);
    double cpu = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("cost:    %.1f ms CPU per second of audio on this PC\n", cpu * 1000 / (n / 16000.0));
    wav_write(argv[2], y, n, 16000);
    return 0;
}
