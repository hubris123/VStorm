/* VStorm virtual screen: LVGL display + touch driver for the browser.
 * JS drives the loop (requestAnimationFrame -> sim_loop) and reads the
 * RGBA framebuffer straight out of wasm memory. */
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "ui.h"
#include "sim_hal.h"

#define EXPORT(n) __attribute__((export_name(n)))

#define MAX_W 480
#define MAX_H 480
static uint32_t s_fb[MAX_W * MAX_H];            /* RGBA8888, read by JS */
static lv_color_t s_buf[MAX_W * 40];            /* LVGL partial draw buffer */
static int s_w = 320, s_h = 172;
static uint32_t s_dirty = 0;

static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static lv_indev_drv_t s_indev_drv;
static int s_tx, s_ty;
static bool s_pressed, s_press_latched; /* latch: a tap shorter than one indev poll still registers */

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px)
{
    for (int y = a->y1; y <= a->y2; y++) {
        uint32_t *row = &s_fb[y * s_w];
        for (int x = a->x1; x <= a->x2; x++, px++) {
            uint32_t c = lv_color_to32(*px);          /* 0xAARRGGBB */
            uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
            row[x] = 0xFF000000u | (b << 16) | (g << 8) | r;   /* little-endian RGBA */
        }
    }
    s_dirty++;
    lv_disp_flush_ready(drv);
}

static void touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *d)
{
    d->point.x = s_tx;
    d->point.y = s_ty;
    d->state = (s_pressed || s_press_latched) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    s_press_latched = false;
}

EXPORT("sim_init") void sim_init(int w, int h)
{
    if (w > MAX_W) w = MAX_W;
    if (h > MAX_H) h = MAX_H;
    s_w = w; s_h = h;

    sim_hal_init();   /* settings / backlight / audio, as the target's app_main */

    lv_init();
    lv_disp_draw_buf_init(&s_draw_buf, s_buf, NULL, MAX_W * 40);
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = w;
    s_disp_drv.ver_res = h;
    s_disp_drv.flush_cb = flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = touch_cb;
    lv_indev_drv_register(&s_indev_drv);

    ui_init();
}

EXPORT("sim_loop") void sim_loop(void) { lv_timer_handler(); }
EXPORT("sim_touch") void sim_touch(int x, int y, int pressed)
{
    s_tx = x; s_ty = y;
    if (pressed && !s_pressed) s_press_latched = true;
    s_pressed = pressed;
}
EXPORT("sim_fb") uint32_t *sim_fb(void) { return s_fb; }
EXPORT("sim_dirty") uint32_t sim_dirty(void) { return s_dirty; }
EXPORT("sim_mem_used") int sim_mem_used(void)
{
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    return (int)(m.total_size - m.free_size);
}

EXPORT("sim_default_w") int sim_default_w(void) { return SIM_DEFAULT_W; }
EXPORT("sim_default_h") int sim_default_h(void) { return SIM_DEFAULT_H; }
