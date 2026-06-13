#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    AUDIO_MODE_PASSTHROUGH = 0,
    AUDIO_MODE_TONE,       /* 440 Hz self-test tone, no mic needed */
    AUDIO_MODE_MUTE,
    AUDIO_MODE_COUNT,
} audio_mode_t;

esp_err_t audio_init(void);
void audio_set_mode(audio_mode_t mode);
audio_mode_t audio_get_mode(void);
/* gains in percent, 100 = unity */
void audio_set_gain_in(int pct);
void audio_set_gain_out(int pct);
/* UI feedback: peak level 0..100 of last block, and clip flag (sticky per read) */
int audio_get_level(void);
bool audio_get_clip(void);
