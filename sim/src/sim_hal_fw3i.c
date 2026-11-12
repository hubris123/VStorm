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

/* simulated sensors, driven by the page's sliders (device: AXP2101 + QMI8658) */
#define EXPORT(n) __attribute__((export_name(n)))
static int s_batt = 78;
static float s_tx, s_ty, s_temp = 31.0f;
static int n_batt, n_temp, n_tilt;   /* read counters for tests */
EXPORT("sim_set_temp") void sim_set_temp(float c) { s_temp = c; }
bool ui_platform_temp(float *c) { n_temp++; *c = s_temp; return true; }
EXPORT("sim_set_battery") void sim_set_battery(int pct) { s_batt = pct; }
EXPORT("sim_set_tilt") void sim_set_tilt(float x, float y) { s_tx = x; s_ty = y; }
EXPORT("sim_dbg_reads") int sim_dbg_reads(int which) { return which == 0 ? n_batt : which == 1 ? n_temp : n_tilt; }
int ui_platform_battery(void) { n_batt++; return s_batt; }
bool ui_platform_tilt(float *x, float *y) { n_tilt++; *x = s_tx; *y = s_ty; return true; }

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
    s->show_batt = 1;
    s->show_tilt = 1;
    s->tilt_quiet = 1;
    s->show_temp = 1;
    s->temp_f = 1;
    s->skin = 0;
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
