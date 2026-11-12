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
    uint8_t show_batt;   /* battery bar beside the meter       */
    uint8_t show_tilt;   /* tilt crosshair lines in the scope  */
    uint8_t tilt_quiet;  /* hide tilt lines while voice is on  */
    uint8_t show_temp;   /* secret temp readout on readout line 1 */
    uint8_t temp_f;      /* 1 = Fahrenheit, 0 = Celsius        */
    uint8_t skin;        /* active skin (0 = built-in default) */
    /* VOICE page: learned shift onto a target voice */
    uint8_t voice_auto;     /* 1 = shift on (needs voice_learned)          */
    uint8_t voice_target;   /* 0 = TROOPER (1 = CLONE, later)               */
    uint8_t voice_learned;  /* a learn has been accepted                    */
    int16_t voice_st100;    /* pitch shift, semitones x 100 (-950 = -9.5 st) */
    int16_t voice_tone10;   /* tone shift, percent x 10 (-180 = -18 %)       */
    int16_t voice_f0;       /* wearer's learned pitch, Hz (for the display)  */
} ui_settings_t;

/* platform hooks */
void ui_platform_load(ui_settings_t *s);         /* saved values, or defaults        */
void ui_platform_defaults(ui_settings_t *s);     /* factory values                   */
void ui_platform_apply(const ui_settings_t *s);  /* push to DSP / codec / backlight  */
                                                 /* (and persist)                    */
/* sensors: battery 0..100 or -1 when no battery (USB only) - AXP2101;
 * tilt in degrees, x = left/right roll, y = forward/back pitch - QMI8658 */
int  ui_platform_battery(void);
bool ui_platform_tilt(float *x_deg, float *y_deg);
bool ui_platform_temp(float *celsius);           /* board temperature - QMI8658 sensor */

/* audio state, beyond firmware3's audio.h */
bool audio_is_keyed(void);                       /* VOX gate open (incl. hang/clicks) */
/* VOICE page: try a shift live without saving (learn / adjust), and hear the output
 * through the speaker while LIVE is muted */
void ui_platform_voice_live(bool on, float pitch_ratio, float formant_ratio);
void ui_platform_voice_monitor(bool on);

/* skin (static background layer, RGB565 368x448) + fonts */
extern const uint8_t skin_main_px[], skin_lock_px[], skin_settings_value_px[],
                     skin_settings_wide_px[], skin_callsign_px[], skin_settings_sensors_px[], skin_settings_skin_px[],
                     skin_settings_voice_big_px[];
extern const uint8_t aurebesh_ttf_start[];
extern const unsigned aurebesh_ttf_len;
extern const uint8_t aureng_ttf_start[];
extern const unsigned aureng_ttf_len;
