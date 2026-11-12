#pragma once
/* Stormtrooper voice DSP: band-pass -> soft-clip -> bitcrush -> static bed,
 * gated by VOX with key-up / key-down click bursts. Pure C, no hardware deps;
 * reads effect parameters from g_settings. */
#include <stdint.h>
#include <stdbool.h>

void dsp_init(int sample_rate_hz);
/* one sample in (Q15 scale as float, after input gain) -> one sample out (Q15).
 * Sets *clipped when the output saturates. */
int16_t dsp_process(float x_q15, bool *clipped);
/* true while the VOX gate is keyed (incl. key-up/down click bursts) */
bool dsp_is_keyed(void);
