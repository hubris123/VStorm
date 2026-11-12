/* Render synthesised key sounds to WAV files (16 kHz mono) for measuring and listening.
 *   gcc -O2 -I firmware3/main tools/dsp_test/render_keys.c firmware3/main/keysnd.c -lm -o rk
 *   ./rk <out dir> [count]          -> out/ours_NN_up.wav / _down.wav + ours_demo.wav */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "keysnd.h"

#define SR 16000

void wav_write(const char *path, const float *x, int n, int rate)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t sz = 36 + n * 2, sr = rate, br = rate * 2, d = n * 2, fmt = 16;
    uint16_t pcm = 1, ch = 1, ba = 2, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&sz, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&d, 4, 1, f);
    for (int i = 0; i < n; i++) {
        float v = x[i] * 32767.0f;
        int16_t s = v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v;
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}

#ifndef NO_MAIN
int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    int count = argc > 2 ? atoi(argv[2]) : 8;
    keysnd_init(SR);
    static float demo[SR * 120];
    int dn = 0;
    char p[512];
    for (int k = 0; k < count; k++) {
        for (int up = 1; up >= 0; up--) {
            static float buf[SR];
            int n = 0;
            for (int i = 0; i < SR / 50; i++) buf[n++] = 0;            /* 20 ms lead-in */
            keysnd_start(up, 0.9f);
            while (keysnd_active() && n < SR - SR / 50) buf[n++] = keysnd_next();
            for (int i = 0; i < SR / 50; i++) buf[n++] = 0;
            static const char *KN[] = {"tock", "stutter", "carrier", "squelch", "chirp", "pops"};
            snprintf(p, sizeof p, "%s/ours_%02d_%s_%s.wav", dir, k + 1, up ? "up" : "down", KN[keysnd_kind() % 6]);
            wav_write(p, buf, n, SR);
            if (dn + n < SR * 119) { memcpy(demo + dn, buf, n * sizeof(float)); dn += n; }
            for (int i = 0; i < SR * 6 / 10 && dn < SR * 119; i++) demo[dn++] = 0;   /* 0.6 s apart */
        }
    }
    snprintf(p, sizeof p, "%s/ours_demo.wav", dir);
    wav_write(p, demo, dn, SR);
    printf("rendered %d key-up + %d key-down sounds to %s\n", count, count, dir);
    return 0;
}
#endif
