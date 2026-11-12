/* Target fw3: ESP32-S3-Touch-AMOLED-1.8 build in firmware3/ (the active board).
 *   ui.c        compiled unmodified from firmware3/main
 *   audio.c     ES8311 I2S full duplex -> Web Audio blocks pushed in by JS.
 *               The DSP math in firmware3/main/audio.c is the same code as
 *               firmware/main/dsp.c, so that file is used here; the four effect
 *               parameters below start at firmware3's values.
 *   GPIO0/BOOT  toggles MUTED <-> LIVE (output boots muted, as on the device)
 *   backlight   SH8601 cmd 0x51 = 140 of 255 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "audio.h"
#include "dsp.h"
#include "settings.h"
#include "esp_random.h"
#include "sim_hal.h"

#define EXPORT(n) __attribute__((export_name(n)))
#define IMPORT(n) __attribute__((import_module("env"), import_name(n)))
IMPORT("js_random") uint32_t js_random(void);
uint32_t esp_random(void) { return js_random(); }

/* dsp.c reads its parameters from g_settings; firmware3 audio.c values: */
settings_t g_settings = {
    .callsign = "TK-421",
    .fx_intensity = 70,   /* s_fx_intensity */
    .vox_thresh = 22,     /* s_vox_thresh   */
    .static_lvl = 15,     /* s_static_lvl   */
    .click_vol = 60,      /* s_click_vol    */
};

static volatile int s_level = 0;
static volatile bool s_clip = false;
static volatile bool s_muted = true;   /* default muted, as firmware3 */
static int s_brightness = 140 * 100 / 255;

int  audio_get_level(void) { return s_level; }
bool audio_get_clip(void)  { bool c = s_clip; s_clip = false; return c; }
void audio_set_mute(bool m) { s_muted = m; }
bool audio_is_muted(void)   { return s_muted; }

static int s_rate = 16000, s_block = 256, s_peak = 0, s_peak_n = 0;
#define AUDIO_MAX_BLOCK 4096
static float s_in[AUDIO_MAX_BLOCK], s_out[AUDIO_MAX_BLOCK];

void sim_hal_init(void) { dsp_init(s_rate); }

EXPORT("sim_get_brightness") int sim_get_brightness(void) { return s_brightness; }
EXPORT("sim_audio_in_ptr")  float *sim_audio_in_ptr(void)  { return s_in; }
EXPORT("sim_audio_out_ptr") float *sim_audio_out_ptr(void) { return s_out; }
EXPORT("sim_audio_set_rate") void sim_audio_set_rate(int hz)
{
    s_rate = hz;
    s_block = hz * 16 / 1000;   /* level window = firmware3's 256-frame block */
    dsp_init(hz);
}
EXPORT("sim_audio_keyed") int sim_audio_keyed(void) { return dsp_is_keyed(); }

/* mirrors firmware3 audio_task: no software input gain (the ES8311 PGA does it),
 * peak -> level, DSP always runs, mute zeroes the output */
EXPORT("sim_audio_process") void sim_audio_process(int n)
{
    if (n > AUDIO_MAX_BLOCK) n = AUDIO_MAX_BLOCK;
    for (int i = 0; i < n; i++) {
        float f = s_in[i];
        if (f > 1.0f) f = 1.0f;
        if (f < -1.0f) f = -1.0f;
        int32_t s = (int32_t)(f * 32767.0f);
        int a = s < 0 ? -s : s;
        if (a > s_peak) s_peak = a;
        if (++s_peak_n >= s_block) {
            s_level = (s_peak * 100) >> 15;
            s_peak = 0; s_peak_n = 0;
        }
        bool c = false;
        int16_t o = dsp_process((float)s, &c);
        if (c) s_clip = true;
        s_out[i] = s_muted ? 0.0f : o / 32768.0f;
    }
}

/* BOOT: single press toggles mute (no lock / reset on this firmware yet) */
EXPORT("sim_boot_click") void sim_boot_click(void) { s_muted = !s_muted; }
EXPORT("sim_boot_lock")  void sim_boot_lock(void)  {}
EXPORT("sim_boot_reset") void sim_boot_reset(void) {}

/* live DSP tuning from the page (values are what goes into firmware3/main/audio.c) */
EXPORT("sim_dsp_set") void sim_dsp_set(int fx, int vox, int stat, int click)
{
    g_settings.fx_intensity = fx;
    g_settings.vox_thresh = vox;
    g_settings.static_lvl = stat;
    g_settings.click_vol = click;
}
