#pragma once
/* The whole trooper voice chain, hardware-free (firmware3 audio task + simulator):
 *   mic -> voice shift (learned pitch + tone) -> radio band-pass -> drive -> bitcrush
 *       -> static bed, gated by VOX, with synthesised key-up / key-down sounds (keysnd.c).
 * Learning (VOICE page) runs inside the voice shift while audio flows. */
#include <stdint.h>
#include <stdbool.h>
#include "vshift.h"

typedef struct {
    int fx;                    /* 0..100 drive + bitcrush           */
    int vox;                   /* 0..100 gate open level            */
    int stat;                  /* 0..100 static bed                 */
    int click;                 /* 0..100 key sound level            */
    bool shift_on;             /* voice shift active                */
    float pitch_ratio;         /* 1.0 = unchanged                   */
    float formant_ratio;       /* 1.0 = unchanged ("TONE")          */
    float f0_ref;              /* wearer's learned pitch, Hz (0 = no range narrowing) */
    float range;               /* pitch swings kept (1.0 = all, 0.7 = 30 % narrower)  */
    int mic_gain;              /* 0..30 dB boost on the mic, before everything        */
    int soft_clip;             /* 0..100: 0 = hard clip, higher = peaks rounded earlier and smoother */
} voicefx_params_t;

void voicefx_init(int rate_hz);
void voicefx_set(const voicefx_params_t *p);
/* one mic sample (Q15 scale as float) -> one output sample (Q15); *clipped on saturation */
int16_t voicefx_process(float x_q15, bool *clipped);
bool voicefx_keyed(void);
void voicefx_tailcut(bool on);   /* cut room echo after words (default on) */      /* VOX gate open (incl. hang and key sounds) */
