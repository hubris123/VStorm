#pragma once
#include <stdint.h>
/* provided by the host (JS in the browser build) */
uint32_t sim_tick_ms(void);
void sim_abort(void);
