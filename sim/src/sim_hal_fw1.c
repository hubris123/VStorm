/* Target fw1: the original ESP32-S3-Touch-LCD-1.47 build in firmware/.
 * Simulator stand-ins for the firmware's hardware-facing modules:
 *   settings.c (NVS)      -> browser localStorage via JS imports
 *   audio.c   (ADC/SDM)   -> Web Audio blocks pushed in by JS, same DSP (dsp.c)
 *   bsp_display brightness-> value read back by JS (CSS filter)
 * Everything else (ui.c, dsp.c) is the unmodified firmware source. */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "audio.h"
#include "dsp.h"
#include "settings.h"
#include "bsp_display.h"
#include "esp_random.h"
#include "ui.h"
#include "sim_hal.h"

#define EXPORT(n) __attribute__((export_name(n)))
#define IMPORT(n) __attribute__((import_module("env"), import_name(n)))

IMPORT("js_settings_load")  int  js_settings_load(void *buf, int len);
IMPORT("js_settings_save")  void js_settings_save(const void *buf, int len);
IMPORT("js_factory_reset")  void js_factory_reset(void);
IMPORT("js_random")         uint32_t js_random(void);

/* ---------- settings (defaults copied from firmware/main/settings.c) ---------- */
settings_t g_settings;
static const settings_t s_defaults = {
    .callsign = "TK-421",
    .gain_in = 100,
    .gain_out = 100,
    .vox_thresh = 20,
    .fx_intensity = 60,
    .static_lvl = 25,
    .click_vol = 70,
    .brightness = 60,
};

void settings_load(void)
{
    g_settings = s_defaults;
    settings_t tmp;
    if (js_settings_load(&tmp, sizeof(tmp)) == (int)sizeof(tmp)) {
        g_settings = tmp;
        g_settings.callsign[CALLSIGN_MAX] = '\0';
    }
}
void settings_save(void) { js_settings_save(&g_settings, sizeof(g_settings)); }
void settings_factory_reset(void) { js_factory_reset(); }

uint32_t esp_random(void) { return js_random(); }

/* ---------- display backlight ---------- */
static int s_brightness = 60;
void bsp_display_set_brightness(int b) { s_brightness = b; }
EXPORT("sim_get_brightness") int sim_get_brightness(void) { return s_brightness; }

/* ---------- audio engine: mirrors audio.c's capture task + output ISR ---------- */
static audio_mode_t s_mode = AUDIO_MODE_PASSTHROUGH;
static int s_gain_in = 100, s_gain_out = 100;
static int s_level = 0;
static bool s_clip = false;
static int s_rate = 16000;
static int s_peak = 0, s_peak_n = 0, s_level_block = 256;
static double s_tone_ph = 0;

esp_err_t audio_init(void) { dsp_init(s_rate); return ESP_OK; }
void audio_set_mode(audio_mode_t m) { s_mode = m; }
audio_mode_t audio_get_mode(void) { return s_mode; }
void audio_set_gain_in(int p) { s_gain_in = p; }
void audio_set_gain_out(int p) { s_gain_out = p; }
int audio_get_level(void) { return s_level; }
bool audio_get_clip(void) { bool c = s_clip; s_clip = false; return c; }

#define AUDIO_MAX_BLOCK 4096
static float s_in[AUDIO_MAX_BLOCK], s_out[AUDIO_MAX_BLOCK];
EXPORT("sim_audio_in_ptr")  float *sim_audio_in_ptr(void)  { return s_in; }
EXPORT("sim_audio_out_ptr") float *sim_audio_out_ptr(void) { return s_out; }

/* the firmware DSP runs at 16 kHz; the browser may force another rate */
EXPORT("sim_audio_set_rate") void sim_audio_set_rate(int hz)
{
    s_rate = hz;
    s_level_block = hz * 16 / 1000;   /* level meter window = firmware's 16 ms block */
    dsp_init(hz);
}

/* process n samples (-1..1) from s_in into s_out */
EXPORT("sim_audio_process") void sim_audio_process(int n)
{
    if (n > AUDIO_MAX_BLOCK) n = AUDIO_MAX_BLOCK;
    for (int i = 0; i < n; i++) {
        /* capture side: mic -> input gain -> Q15 clip + peak (as capture_task) */
        int32_t s = (int32_t)(s_in[i] * 32767.0f);
        s = (s * s_gain_in) / 100;
        if (s > 32767) { s = 32767; s_clip = true; }
        if (s < -32768) { s = -32768; s_clip = true; }
        int a = s < 0 ? -s : s;
        if (a > s_peak) s_peak = a;
        if (++s_peak_n >= s_level_block) {
            s_level = (s_peak * 100) >> 15;
            s_peak = 0; s_peak_n = 0;
        }

        /* output side: as out_timer_cb */
        int32_t o = 0;
        if (s_mode == AUDIO_MODE_PASSTHROUGH) {
            bool c = false;
            o = dsp_process((float)s, &c);
            if (c) s_clip = true;
        } else if (s_mode == AUDIO_MODE_TONE) {
            s_tone_ph += 440.0 / s_rate;
            if (s_tone_ph >= 1.0) s_tone_ph -= 1.0;
            o = (int32_t)(sin(s_tone_ph * 2 * M_PI) * 32767.0) / 4;
        }
        o = (o * s_gain_out) / 100;
        if (o > 32767) o = 32767;
        if (o < -32768) o = -32768;
        s_out[i] = o / 32768.0f;
    }
}

EXPORT("sim_audio_keyed") int sim_audio_keyed(void) { return dsp_is_keyed(); }

void sim_hal_init(void)
{
    /* same order as firmware/main/main.c app_main */
    settings_load();
    bsp_display_set_brightness(g_settings.brightness);
    audio_init();
}

/* BOOT button (firmware: click = mute, 3 s = lock, 10 s = factory reset) */
EXPORT("sim_boot_click") void sim_boot_click(void) { ui_request_mute_toggle(); }
EXPORT("sim_boot_lock")  void sim_boot_lock(void)  { ui_request_lock(); }
EXPORT("sim_boot_reset") void sim_boot_reset(void) { ui_request_factory_reset(); }
EXPORT("sim_dsp_set") void sim_dsp_set(int fx, int vox, int stat, int click) { (void)fx; (void)vox; (void)stat; (void)click; }
