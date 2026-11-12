#pragma once
/* What the Imperial UI needs from the platform (firmware3 or the simulator). */
#include <stdbool.h>
#include <stdint.h>

#define UI_CALLSIGN_MAX 10

typedef struct {
    char callsign[UI_CALLSIGN_MAX + 1];
    int volume;      /* 0..100 speaker level   */
    int effect;      /* 0..100 drive+bitcrush  */
    int vox;         /* 0..100 gate threshold  */
    int stat;        /* 0..100 static bed      */
    int click;       /* 0..100 key click level */
    int brightness;  /* 10..100 panel          */
} ui_settings_t;

/* platform hooks */
void ui_platform_load(ui_settings_t *s);         /* saved values, or defaults        */
void ui_platform_defaults(ui_settings_t *s);     /* factory values                   */
void ui_platform_apply(const ui_settings_t *s);  /* push to DSP / codec / backlight  */
                                                 /* (and persist)                    */
/* audio state, beyond firmware3's audio.h */
bool audio_is_keyed(void);                       /* VOX gate open (incl. hang/clicks) */

/* skin (static background layer, RGB565 368x448) + fonts */
extern const uint8_t skin_main_px[], skin_lock_px[], skin_settings_value_px[],
                     skin_settings_wide_px[], skin_callsign_px[];
extern const uint8_t aurebesh_ttf_start[];
extern const unsigned aurebesh_ttf_len;
extern const uint8_t aureng_ttf_start[];
extern const unsigned aureng_ttf_len;
