/* Debug-only serial tap injection. Commands over USB-Serial-JTAG:
 *   tap X Y [MS]   inject a touch press at X,Y for MS ms (default 120)
 *   cal on|off     show/hide 40 px calibration grid
 * While an injected press is active an amber circle marker is drawn at the
 * point on the system layer so the webcam can verify alignment.
 * Remove this file (and its init call) for production builds. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"

#define TAG "ui_debug"

static volatile int32_t inj_x, inj_y;
static volatile uint32_t inj_until;

static lv_obj_t *marker;
static lv_obj_t *grid;

static void inj_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    data->point.x = inj_x;
    data->point.y = inj_y;
    data->state = (lv_tick_get() < inj_until) ? LV_INDEV_STATE_PRESSED
                                              : LV_INDEV_STATE_RELEASED;
}

static void marker_timer_cb(lv_timer_t *t)
{
    bool active = lv_tick_get() < inj_until;
    if (active) {
        lv_obj_clear_flag(marker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(marker, inj_x - 13, inj_y - 13);
    } else {
        lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
    }
}

static void grid_set(bool on)
{
    if (on && !grid) {
        grid = lv_obj_create(lv_layer_sys());
        lv_obj_set_size(grid, 320, 172);
        lv_obj_set_pos(grid, 0, 0);
        lv_obj_set_style_bg_opa(grid, LV_OPA_0, 0);
        lv_obj_set_style_border_width(grid, 0, 0);
        lv_obj_set_style_pad_all(grid, 0, 0);
        lv_obj_clear_flag(grid, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        for (int x = 40; x < 320; x += 40) {
            lv_obj_t *v = lv_obj_create(grid);
            lv_obj_set_size(v, 1, 172);
            lv_obj_set_pos(v, x, 0);
            lv_obj_set_style_bg_color(v, lv_color_hex(0x3A6E96), 0);
            lv_obj_set_style_border_width(v, 0, 0);
            lv_obj_set_style_radius(v, 0, 0);
        }
        for (int y = 40; y < 172; y += 40) {
            lv_obj_t *h = lv_obj_create(grid);
            lv_obj_set_size(h, 320, 1);
            lv_obj_set_pos(h, 0, y);
            lv_obj_set_style_bg_color(h, lv_color_hex(0x3A6E96), 0);
            lv_obj_set_style_border_width(h, 0, 0);
            lv_obj_set_style_radius(h, 0, 0);
        }
    } else if (!on && grid) {
        lv_obj_del(grid);
        grid = NULL;
    }
}

static void handle_line(char *line)
{
    int x, y, ms = 120;
    if (sscanf(line, "tap %d %d %d", &x, &y, &ms) >= 2) {
        if (lvgl_port_lock(200)) {
            inj_x = x; inj_y = y;
            inj_until = lv_tick_get() + ms;
            lvgl_port_unlock();
        }
        ESP_LOGI(TAG, "tap %d %d for %d ms", x, y, ms);
    } else if (strncmp(line, "cal on", 6) == 0) {
        if (lvgl_port_lock(200)) { grid_set(true); lvgl_port_unlock(); }
        ESP_LOGI(TAG, "cal grid on");
    } else if (strncmp(line, "cal off", 7) == 0) {
        if (lvgl_port_lock(200)) { grid_set(false); lvgl_port_unlock(); }
        ESP_LOGI(TAG, "cal grid off");
    } else if (line[0]) {
        ESP_LOGW(TAG, "unknown cmd: %s", line);
    }
}

static void serial_task(void *arg)
{
    static char line[64];
    int pos = 0;
    uint8_t buf[32];
    while (1) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(100));
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\n' || c == '\r') {
                line[pos] = '\0';
                if (pos > 0) handle_line(line);
                pos = 0;
            } else if (pos < (int)sizeof(line) - 1) {
                line[pos++] = c;
            }
        }
    }
}

void ui_debug_init(void)
{
    usb_serial_jtag_driver_config_t cfg = {
        .rx_buffer_size = 256,
        .tx_buffer_size = 256,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));

    if (lvgl_port_lock(0)) {
        /* synthetic pointer input device */
        static lv_indev_drv_t drv;
        lv_indev_drv_init(&drv);
        drv.type = LV_INDEV_TYPE_POINTER;
        drv.read_cb = inj_read_cb;
        lv_indev_drv_register(&drv);

        /* amber marker circle, shown while an injected press is active */
        marker = lv_obj_create(lv_layer_sys());
        lv_obj_set_size(marker, 26, 26);
        lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(marker, lv_color_hex(0xEEB52A), 0);
        lv_obj_set_style_border_width(marker, 2, 0);
        lv_obj_set_style_border_color(marker, lv_color_hex(0xE8E8E8), 0);
        lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(marker, LV_OBJ_FLAG_CLICKABLE);
        lv_timer_create(marker_timer_cb, 30, NULL);
        lvgl_port_unlock();
    }

    xTaskCreate(serial_task, "ui_debug", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "serial tap injection ready (tap X Y [MS] | cal on|off)");
}
