#pragma once
/* Synthesised radio key-up / key-down sounds (see keysnd.c). Pure C, no hardware. */
#include <stdbool.h>

void keysnd_init(int rate_hz);
/* start a sound: keyup = true at the start of a transmission, false at the end; volume 0..1 */
void keysnd_start(bool keyup, float volume);
/* next output sample (-1..1 scaled by volume); 0 when idle */
float keysnd_next(void);
bool keysnd_active(void);
