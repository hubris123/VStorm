#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_log.h"
#include "settings.h"

#define NVS_NS  "vc"
#define NVS_KEY "settings"

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
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(settings_t);
        settings_t tmp;
        if (nvs_get_blob(h, NVS_KEY, &tmp, &len) == ESP_OK && len == sizeof(settings_t)) {
            g_settings = tmp;
            g_settings.callsign[CALLSIGN_MAX] = '\0';
        }
        nvs_close(h);
    }
}

void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, NVS_KEY, &g_settings, sizeof(settings_t));
        nvs_commit(h);
        nvs_close(h);
    }
}

void settings_factory_reset(void)
{
    nvs_flash_erase();
    esp_restart();
}
