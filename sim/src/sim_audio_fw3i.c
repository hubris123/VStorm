/* Target fw3i: the audio side of the Imperial build. Runs the new firmware3 voice chain
 * (firmware3/main/voicefx.c: voice shift -> radio effect -> VOX + synthesised key sounds)
 * on Web Audio blocks pushed in by JS, mirroring the firmware3 audio task:
 * peak -> level, the chain always runs, mute zeroes the output (unless monitoring). */
#include <stdint.h>
#include <stdbool.h>
#include "audio.h"
#include "voicefx.h"
#include "esp_random.h"
#include "sim_hal.h"

#define EXPORT(n) __attribute__((export_name(n)))
#define IMPORT(n) __attribute__((import_module("env"), import_name(n)))
IMPORT("js_random") uint32_t js_random(void);
uint32_t esp_random(void) { return js_random(); }

static volatile int s_level;
static volatile bool s_clip;
static volatile bool s_muted = true;       /* boots muted, as firmware3 */
static bool s_monitor;                     /* hear the output while muted (VOICE learn / adjust) */
static int s_volume = 55, s_brightness = 55;
static voicefx_params_t s_fx = {70, 22, 15, 60, false, 1.0f, 1.0f};

int  audio_get_level(void) { return s_level; }
bool audio_get_clip(void)  { bool c = s_clip; s_clip = false; return c; }
void audio_set_mute(bool m) { s_muted = m; }
bool audio_is_muted(void)   { return s_muted; }
bool audio_is_keyed(void)   { return voicefx_keyed(); }

/* called by the platform hooks (sim_hal_fw3i.c) */
void sim_audio_params(int volume, int brightness, const voicefx_params_t *fx)
{
    s_volume = volume;
    s_brightness = brightness;
    s_fx = *fx;
    voicefx_set(&s_fx);
}
void sim_audio_monitor(bool on) { s_monitor = on; }

static int s_rate = 16000, s_block = 256, s_peak, s_peak_n;
#define AUDIO_MAX_BLOCK 4096
static float s_in[AUDIO_MAX_BLOCK], s_out[AUDIO_MAX_BLOCK];

void sim_hal_init(void) { voicefx_init(s_rate); voicefx_set(&s_fx); }

EXPORT("sim_get_brightness") int sim_get_brightness(void) { return s_brightness; }
EXPORT("sim_audio_in_ptr")  float *sim_audio_in_ptr(void)  { return s_in; }
EXPORT("sim_audio_out_ptr") float *sim_audio_out_ptr(void) { return s_out; }
EXPORT("sim_audio_set_rate") void sim_audio_set_rate(int hz)
{
    s_rate = hz;
    s_block = hz * 16 / 1000;              /* level window = firmware3's 256-frame block */
    voicefx_init(hz);
    voicefx_set(&s_fx);
}
EXPORT("sim_audio_keyed") int sim_audio_keyed(void) { return voicefx_keyed(); }

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
        int16_t o = voicefx_process((float)s, &c);
        if (c) s_clip = true;
        s_out[i] = (s_muted && !s_monitor) ? 0.0f : o / 32768.0f * s_volume / 55.0f;
    }
}

EXPORT("sim_boot_click") void sim_boot_click(void) { s_muted = !s_muted; }
EXPORT("sim_boot_lock")  void sim_boot_lock(void)  {}
EXPORT("sim_boot_reset") void sim_boot_reset(void) {}
/* the page's tuning card (fw3 only) */
EXPORT("sim_dsp_set") void sim_dsp_set(int fx, int vox, int stat, int click)
{
    s_fx.fx = fx; s_fx.vox = vox; s_fx.stat = stat; s_fx.click = click;
    voicefx_set(&s_fx);
}
