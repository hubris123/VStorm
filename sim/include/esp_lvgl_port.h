#pragma once
/* sim: single-threaded, so the LVGL mutex is a no-op */
#include <stdbool.h>
#include <stdint.h>
static inline bool lvgl_port_lock(uint32_t timeout_ms) { (void)timeout_ms; return true; }
static inline void lvgl_port_unlock(void) {}
