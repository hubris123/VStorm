/* Target fw3i: platform hooks for the Imperial UI (sim/ui_imperial/) on top of the
 * firmware3 stand-ins in sim_hal_fw3.c. Settings persist in browser localStorage. */
#include <string.h>
#include "ui_imperial.h"
#include "settings.h"
#include "dsp.h"

#define IMPORT(n) __attribute__((import_module("env"), import_name(n)))
IMPORT("js_settings_load") int js_settings_load(void *buf, int len);
IMPORT("js_settings_save") void js_settings_save(const void *buf, int len);

bool audio_is_keyed(void) { return dsp_is_keyed(); }

void ui_platform_defaults(ui_settings_t *s)
{
    memset(s, 0, sizeof(*s));
    strcpy(s->callsign, "TK-421");
    s->volume = 55;      /* ES8311 voice volume today */
    s->effect = 70;      /* firmware3 audio.c values  */
    s->vox = 22;
    s->stat = 15;
    s->click = 60;
    s->brightness = 55;  /* SH8601 0x51 = 140 */
}

void ui_platform_load(ui_settings_t *s)
{
    ui_platform_defaults(s);
    ui_settings_t tmp;
    if (js_settings_load(&tmp, sizeof(tmp)) == (int)sizeof(tmp)) {
        tmp.callsign[UI_CALLSIGN_MAX] = 0;
        *s = tmp;
    }
}

void ui_platform_apply(const ui_settings_t *s)
{
    g_settings.gain_out = s->volume;
    g_settings.fx_intensity = s->effect;
    g_settings.vox_thresh = s->vox;
    g_settings.static_lvl = s->stat;
    g_settings.click_vol = s->click;
    g_settings.brightness = s->brightness;
    memcpy(g_settings.callsign, s->callsign, sizeof(g_settings.callsign));
    js_settings_save(s, sizeof(*s));
}
