#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "iot_button.h"
#include "button_gpio.h"

#include "bsp/esp32_s3_touch_amoled_1_8.h"
#include "esp_lv_adapter.h"

#include "audio.h"
#include "settings.h"
#include "ui.h"

static const char *TAG = "main";

static void btn_mute_cb(void *arg, void *data)   { ui_request_mute_toggle(); }
static void btn_lock_cb(void *arg, void *data)   { ui_request_lock(); }
static void btn_freset_cb(void *arg, void *data) { ui_request_factory_reset(); }

static void button_init(void)
{
    button_config_t btn_cfg = {};
    button_gpio_config_t btn_gpio_cfg = {};
    btn_gpio_cfg.gpio_num = GPIO_NUM_0;
    btn_gpio_cfg.active_level = 0;
    static button_handle_t btn = NULL;
    ESP_ERROR_CHECK(iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &btn));
    iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, NULL, btn_mute_cb, NULL);
    button_event_args_t lock_args = { .long_press.press_time = 3000 };
    iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, &lock_args, btn_lock_cb, NULL);
    button_event_args_t freset_args = { .long_press.press_time = 10000 };
    iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, &freset_args, btn_freset_cb, NULL);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    settings_load();

    /* BSP inits QSPI AMOLED, CST816S touch, TCA9554 expander, and LVGL */
    lv_display_t *disp = bsp_display_start();
    if (!disp) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }
    bsp_display_brightness_set(g_settings.brightness);

    button_init();
    ESP_ERROR_CHECK(audio_init());
    audio_set_mode(AUDIO_MODE_PASSTHROUGH);

    if (bsp_display_lock(0)) {
        ui_init();
        /* Force the initial full repaint, matching the proven 05_LVGL_WITH_RAM
         * example. Without this the adapter's first frame leaves stale
         * framebuffer regions (garbage behind the UI). */
        lv_obj_invalidate(lv_scr_act());
        bsp_display_unlock();
        ESP_ERROR_CHECK(esp_lv_adapter_refresh_now(disp));
    }

    extern void ui_debug_init(void);
    ui_debug_init();
}
