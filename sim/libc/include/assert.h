#pragma once
void sim_abort(void);
#define assert(x) ((x) ? (void)0 : sim_abort())
