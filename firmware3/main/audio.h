#pragma once
#include "esp_err.h"
#include "driver/i2c_master.h"
#include <stdbool.h>

/* Pass the shared main I2C bus (ES8311 control lives on it). */
esp_err_t audio_init(i2c_master_bus_handle_t i2c_bus);

int  audio_get_level(void);   /* 0..100 peak of last block */
bool audio_get_clip(void);    /* true if clipped since last call (self-clears) */

void audio_set_mute(bool m);  /* mute speaker output (mic capture still runs) */
bool audio_is_muted(void);
