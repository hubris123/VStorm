#pragma once
#include <stdint.h>

#define CALLSIGN_MAX 10

typedef struct {
    char callsign[CALLSIGN_MAX + 1];
    uint16_t gain_in;     /* 0..300 % */
    uint16_t gain_out;    /* 0..300 % */
    uint8_t vox_thresh;   /* 0..100 level */
    uint8_t fx_intensity; /* 0..100 */
    uint8_t static_lvl;   /* 0..100 */
    uint8_t click_vol;    /* 0..100 */
    uint8_t brightness;   /* 10..100 */
} settings_t;

extern settings_t g_settings;

void settings_load(void);
void settings_save(void);
void settings_factory_reset(void); /* erase NVS and reboot */
