#pragma once
/* Synthesised radio key-up / key-down sounds (see keysnd.c). Pure C, no hardware. */
#include <stdbool.h>

void keysnd_init(int rate_hz);
/* start a sound: keyup = true at the start of a transmission, false at the end; volume 0..1 */
void keysnd_start(bool keyup, float volume);
/* next output sample (-1..1 scaled by volume); 0 when idle */
float keysnd_next(void);
bool keysnd_active(void);
/* key-up: samples from the start until the main click has passed (the voice comes in after) */
int keysnd_head(void);
int keysnd_kind(void);        /* last sound kind (0 tock, 1 stutter, 2 carrier, 3 squelch, 4 chirp, 5 pops) */
