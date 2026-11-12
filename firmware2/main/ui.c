/* Voice changer UI: main / settings / callsign / lock screens.
 * Imperial theme: dark backgrounds, red+cyan accents.
 * Portrait 368x448 layout for ESP32-S3-Touch-AMOLED-1.8. */
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "esp_random.h"
#include "esp_log.h"
#include "audio.h"
#include "settings.h"
#include "bsp/esp32_s3_touch_amoled_1_8.h"
#include "ui.h"

/* display dimensions */
#define LCD_W 368
#define LCD_H 448

/* drab Imperial palette */
#define COL_BG     lv_color_hex(0x000000)
#define COL_PANEL  lv_color_hex(0x0C0F12)
#define COL_RED    lv_color_hex(0x9B1B1B)
#define COL_CYAN   lv_color_hex(0x3A6E96)
#define COL_DIM    lv_color_hex(0x6E6E6E)
#define COL_GREEN  lv_color_hex(0x3C7A3C)
#define COL_GREY   lv_color_hex(0x8A8A8A)
#define COL_ORANGE lv_color_hex(0xEEB52A)
#define COL_WHITE  lv_color_hex(0xE8E8E8)

static lv_obj_t *scr_main, *scr_settings, *scr_callsign, *scr_lock, *scr_audio, *scr_bright;
static lv_obj_t *audio_chart;
static lv_chart_series_t *audio_ser;
static lv_obj_t *wave_chart, *tx_lamp, *clip_lamp, *main_callsign_lbl, *mute_btn_lbl;
static lv_chart_series_t *wave_ser;
static lv_obj_t *lock_wave, *lock_callsign, *lock_hex, *lock_bars[5];
static lv_obj_t *lock_tx_lamp, *lock_clip_lamp;
static lv_chart_series_t *lock_ser;
static lv_font_t *aurebesh_28, *aurebesh_16;
static lv_font_t *callsign_font_20, *callsign_font_28;
static bool s_muted = false;
static audio_mode_t s_premute_mode = AUDIO_MODE_PASSTHROUGH;

#define UNLOCK_TAPS 5
#define UNLOCK_WINDOW_MS 2500
static uint32_t s_tap_times[UNLOCK_TAPS];
static int s_tap_idx = 0;

extern const uint8_t aurebesh_ttf_start[] asm("_binary_aurebesh_ttf_start");
extern const uint8_t aurebesh_ttf_end[]   asm("_binary_aurebesh_ttf_end");
extern const uint8_t aureng_ttf_start[] asm("_binary_aurebesh_english_ttf_start");
extern const uint8_t aureng_ttf_end[]   asm("_binary_aurebesh_english_ttf_end");

/* ---------- shared helpers ---------- */

static lv_obj_t *make_btn(lv_obj_t *parent, int w, int h, lv_color_t border,
                          lv_event_cb_t cb)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_bg_color(b, COL_PANEL, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, border, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static void slider_square(lv_obj_t *sl)
{
    lv_obj_set_style_radius(sl, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(sl, 0, LV_PART_INDICATOR);
    lv_obj_set_style_radius(sl, 0, LV_PART_KNOB);
    lv_obj_set_style_bg_color(sl, COL_DIM, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, COL_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, COL_GREY, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 2, LV_PART_KNOB);
}

/* fake console lights */
#define FAKE_LIGHTS 7
#define FAKE_SETS 2
static lv_obj_t *fake_lights[FAKE_SETS][FAKE_LIGHTS];
static uint8_t fake_state[FAKE_SETS][FAKE_LIGHTS];
static uint32_t fake_rng = 0xDEAD17;

#define FAKE_GREY   lv_color_hex(0x979B9A)
#define FAKE_GREEN  lv_color_hex(0x478544)
#define FAKE_RED    lv_color_hex(0xD00A1B)
#define FAKE_BLUE   lv_color_hex(0x459FC2)
#define FAKE_ORANGE lv_color_hex(0xBF5B16)

static lv_color_t fake_state_color(uint8_t st)
{
    switch (st) {
    case 1: return FAKE_ORANGE;
    case 2: return FAKE_RED;
    case 3: return FAKE_BLUE;
    default: return FAKE_GREY;
    }
}

static uint16_t fake_next[FAKE_SETS][FAKE_LIGHTS];
static uint16_t fake_next_delay(void)
{
    fake_rng = fake_rng * 1664525u + 1013904223u;
    return 200 + ((fake_rng >> 8) % 201);
}

static void fake_lights_timer_cb(lv_timer_t *t)
{
    static int tick = 0;
    tick++;
    for (int s = 0; s < FAKE_SETS; s++) {
        for (int i = 0; i < FAKE_LIGHTS; i++) {
            if (!fake_lights[s][i]) continue;
            if (--fake_next[s][i] == 0) {
                fake_rng = fake_rng * 1664525u + 1013904223u;
                fake_state[s][i] = (fake_state[s][i] + 1 + ((fake_rng >> 8) % 3)) % 4;
                fake_next[s][i] = fake_next_delay();
            }
            lv_obj_set_style_bg_color(fake_lights[s][i],
                                      fake_state_color(fake_state[s][i]), 0);
            if (fake_state[s][i] == 2) {
                int ph = (tick + (s * 3 + i) * 8) % 60;
                int tri = ph < 30 ? ph : 60 - ph;
                lv_obj_set_style_bg_opa(fake_lights[s][i], 120 + tri * 4, 0);
            } else {
                lv_obj_set_style_bg_opa(fake_lights[s][i], LV_OPA_COVER, 0);
            }
        }
    }
}

/* Portrait layout: lights cluster at left side of console panel row
 * x_base/y_base = top-left of the cluster bounding box */
static void make_fake_cluster(lv_obj_t *parent, int set, int x_base, int y_base)
{
    static const struct { int x, y, w, h; bool dome; } fl[FAKE_LIGHTS] = {
        {0,  0,  80, 14, false},
        {0,  22, 32, 14, false}, {42, 22, 28, 28, true},
        {0,  44, 32, 14, false}, {42, 44, 32, 14, false},
        {0,  66, 28, 28, true},  {42, 66, 32, 14, false},
    };
    for (int i = 0; i < FAKE_LIGHTS; i++) {
        lv_obj_t *f = lv_obj_create(parent);
        lv_obj_set_size(f, fl[i].w, fl[i].h);
        lv_obj_set_style_radius(f, fl[i].dome ? LV_RADIUS_CIRCLE : 0, 0);
        lv_obj_set_style_border_width(f, 0, 0);
        lv_obj_clear_flag(f, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(f, LV_ALIGN_TOP_LEFT, x_base + fl[i].x, y_base + fl[i].y);
        fake_state[set][i] = esp_random() % 4;
        lv_obj_set_style_bg_color(f, fake_state_color(fake_state[set][i]), 0);
        fake_next[set][i] = fake_next_delay();
        fake_lights[set][i] = f;
    }
}

static lv_obj_t *knob_outer[FAKE_SETS][3], *knob_inner[FAKE_SETS];
static int knob_outer_rot[FAKE_SETS] = {0, 40};
static int knob_inner_rot[FAKE_SETS] = {90, 200};
static int knob_outer_spd[FAKE_SETS] = {1, -1};
static int knob_inner_spd[FAKE_SETS] = {-2, 2};

static lv_obj_t *make_fake_arc(lv_obj_t *parent, int size, int x, int y,
                               int a0, int a1, int width, lv_color_t col)
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_set_size(a, size, size);
    lv_obj_set_pos(a, x, y);
    lv_arc_set_angles(a, a0, a1);
    lv_obj_set_style_arc_opa(a, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, col, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, false, LV_PART_INDICATOR);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    return a;
}

static void fake_knob_timer_cb(lv_timer_t *t)
{
    for (int s = 0; s < FAKE_SETS; s++) {
        if (!knob_inner[s]) continue;
        fake_rng = fake_rng * 1664525u + 1013904223u;
        if ((fake_rng & 0x7F) == 0) {
            int v = 1 + (int)((fake_rng >> 8) % 3);
            knob_outer_spd[s] = (fake_rng & 0x100) ? v : -v;
        }
        if ((fake_rng & 0x7F) == 1) {
            int v = 1 + (int)((fake_rng >> 16) % 3);
            knob_inner_spd[s] = (fake_rng & 0x100) ? v : -v;
        }
        knob_outer_rot[s] = (knob_outer_rot[s] + knob_outer_spd[s] + 360) % 360;
        knob_inner_rot[s] = (knob_inner_rot[s] + knob_inner_spd[s] + 360) % 360;
        for (int i = 0; i < 3; i++)
            lv_arc_set_rotation(knob_outer[s][i], knob_outer_rot[s] + i * 120);
        lv_arc_set_rotation(knob_inner[s], knob_inner_rot[s]);
    }
}

static void make_fake_knob(lv_obj_t *parent, int set, int x, int y)
{
    for (int i = 0; i < 3; i++)
        knob_outer[set][i] = make_fake_arc(parent, 80, x, y, 0, 80, 8, COL_GREY);
    knob_inner[set] = make_fake_arc(parent, 44, x + 18, y + 18, 0, 250, 7, COL_DIM);

    lv_obj_t *accent = lv_obj_create(parent);
    lv_obj_set_size(accent, 80, 12);
    lv_obj_set_style_radius(accent, 0, 0);
    lv_obj_set_style_bg_color(accent, COL_DIM, 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(accent, LV_ALIGN_TOP_LEFT, x, y + 86);
}

static lv_obj_t *make_screen(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);

    /* The screen root's own background does not reliably paint through the
     * adapter (leaves stale-framebuffer garbage in uncovered areas). Add an
     * explicit full-screen opaque backplate child - a real widget that fills
     * the whole panel, the same way the stock demo's solid background does. */
    lv_obj_t *bg = lv_obj_create(s);
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, COL_BG, 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_background(bg);
    return s;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *txt, lv_color_t col)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, col, 0);
    return l;
}

/* segmented level meter */
#define METER_SEGS 12
#define METER_RED_FROM 9
typedef struct { lv_obj_t *seg[METER_SEGS]; } seg_meter_t;

static void make_seg_meter(seg_meter_t *m, lv_obj_t *parent, int x_ofs, int y_ofs,
                           lv_align_t align, int seg_w, int seg_h)
{
    int step = seg_h + 3;
    for (int i = 0; i < METER_SEGS; i++) {
        lv_obj_t *s = lv_obj_create(parent);
        lv_obj_set_size(s, seg_w, seg_h);
        lv_obj_set_style_radius(s, 0, 0);
        lv_obj_set_style_border_width(s, 0, 0);
        lv_obj_set_style_bg_color(s, COL_PANEL, 0);
        lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(s, align, x_ofs, y_ofs - i * step);
        m->seg[i] = s;
    }
}

static void seg_meter_set(seg_meter_t *m, int level)
{
    int lit = (level * METER_SEGS + 50) / 100;
    for (int i = 0; i < METER_SEGS; i++) {
        lv_color_t c = COL_PANEL;
        if (i < lit) c = (i >= METER_RED_FROM) ? COL_RED : COL_GREEN;
        lv_obj_set_style_bg_color(m->seg[i], c, 0);
    }
}

static seg_meter_t main_meter, lock_meter;

/* confirm dialog */
static lv_obj_t *confirm_box;
static void (*confirm_action)(void);

static void confirm_btn_cb(lv_event_t *e)
{
    bool ok = (bool)(uintptr_t)lv_event_get_user_data(e);
    void (*act)(void) = confirm_action;
    lv_obj_del(confirm_box);
    confirm_box = NULL;
    if (ok && act) act();
}

static void show_confirm(const char *title, const char *body,
                         const char *ok_txt, void (*action)(void))
{
    if (confirm_box) lv_obj_del(confirm_box);
    confirm_action = action;

    confirm_box = lv_obj_create(lv_layer_top());
    lv_obj_set_size(confirm_box, 320, 140);
    lv_obj_center(confirm_box);
    lv_obj_set_style_radius(confirm_box, 0, 0);
    lv_obj_set_style_bg_color(confirm_box, COL_PANEL, 0);
    lv_obj_set_style_border_width(confirm_box, 2, 0);
    lv_obj_set_style_border_color(confirm_box, COL_GREY, 0);
    lv_obj_set_style_pad_all(confirm_box, 6, 0);
    lv_obj_clear_flag(confirm_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = make_label(confirm_box, title, COL_ORANGE);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *b = make_label(confirm_box, body, COL_ORANGE);
    lv_obj_set_width(b, 298);
    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 28);

    const char *txts[2] = {ok_txt, "CANCEL"};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *btn = lv_btn_create(confirm_box);
        lv_obj_set_size(btn, 130, 36);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2E3338), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, COL_GREY, 0);
        lv_obj_align(btn, i == 0 ? LV_ALIGN_BOTTOM_LEFT : LV_ALIGN_BOTTOM_RIGHT,
                     i == 0 ? 6 : -6, -2);
        lv_obj_add_event_cb(btn, confirm_btn_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(i == 0));
        lv_obj_t *l = make_label(btn, txts[i], COL_ORANGE);
        lv_obj_center(l);
    }
}

/* slider row helper */
typedef void (*slider_cb_t)(int);
static void slider_evt(lv_event_t *e)
{
    slider_cb_t cb = (slider_cb_t)lv_event_get_user_data(e);
    cb(lv_slider_get_value(lv_event_get_target(e)));
}
static void make_slider_row(lv_obj_t *parent, const char *name, int min, int max,
                            int val, slider_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), 38);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 2, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = make_label(row, name, COL_CYAN);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *sl = lv_slider_create(row);
    slider_square(sl);
    lv_obj_set_size(sl, 230, 12);
    lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_slider_set_range(sl, min, max);
    lv_slider_set_value(sl, val, LV_ANIM_OFF);
    lv_obj_add_event_cb(sl, slider_evt, LV_EVENT_VALUE_CHANGED, (void *)cb);
}

/* ---------- main screen ---------- */

static void mute_toggle(void)
{
    s_muted = !s_muted;
    if (s_muted) {
        s_premute_mode = audio_get_mode();
        audio_set_mode(AUDIO_MODE_MUTE);
        lv_label_set_text(mute_btn_lbl, "MUTED");
        lv_obj_set_style_text_color(mute_btn_lbl, COL_RED, 0);
    } else {
        audio_set_mode(s_premute_mode);
        lv_label_set_text(mute_btn_lbl, "LIVE");
        lv_obj_set_style_text_color(mute_btn_lbl, FAKE_BLUE, 0);
    }
}
static void mute_btn_cb(lv_event_t *e) { mute_toggle(); }

static void gain_in_cb(int v)  { g_settings.gain_in = v;  audio_set_gain_in(v);  settings_save(); }
static void gain_out_cb(int v) { g_settings.gain_out = v; audio_set_gain_out(v); settings_save(); }

static void goto_settings_cb(lv_event_t *e) { lv_scr_load(scr_settings); }

static void do_lock(void)
{
    s_tap_idx = 0;
    lv_label_set_text(lock_callsign, g_settings.callsign);
    lv_scr_load(scr_lock);
}
static void lock_btn_cb(lv_event_t *e)
{
    show_confirm("LOCK CONSOLE", "Unlock with 5 rapid taps on the screen.",
                 "LOCK", do_lock);
}

static void main_timer_cb(lv_timer_t *t)
{
    int lvl = audio_get_level();
    lv_chart_set_next_value(wave_chart, wave_ser, lvl);
    seg_meter_set(&main_meter, lvl);
    bool tx = lvl > g_settings.vox_thresh;
    lv_obj_set_style_bg_color(tx_lamp, tx ? COL_RED : COL_PANEL, 0);
    lv_obj_set_style_bg_color(clip_lamp, audio_get_clip() ? COL_RED : COL_PANEL, 0);
    if (lv_scr_act() == scr_lock) {
        lv_chart_set_next_value(lock_wave, lock_ser, lvl);
        seg_meter_set(&lock_meter, lvl);
        lv_obj_set_style_bg_color(lock_tx_lamp, tx ? COL_RED : COL_PANEL, 0);
        lv_obj_set_style_bg_color(lock_clip_lamp,
            audio_get_clip() ? COL_RED : COL_PANEL, 0);
    }
    if (lv_scr_act() == scr_audio) {
        lv_chart_set_next_value(audio_chart, audio_ser, lvl);
    }
}

/* Portrait main screen layout (368x448):
 *  y=0..38    : top bar - callsign left, settings gear right
 *  y=42..166  : waveform scope 290x110 + notched outline + seg meter right
 *  y=174..284 : console panel row - fake lights left + knob right
 *  y=292..420 : empty / future use (gain sliders can move here)
 *  y=406..446 : bottom bar - LOCK left, TX/CLIP center, LIVE right
 */
static void build_main(void)
{
    scr_main = make_screen();

    /* top bar */
    main_callsign_lbl = make_label(scr_main, g_settings.callsign, COL_RED);
    lv_obj_set_style_text_font(main_callsign_lbl,
        callsign_font_20 ? callsign_font_20 : &lv_font_montserrat_20, 0);
    lv_obj_align(main_callsign_lbl, LV_ALIGN_TOP_LEFT, 8, 6);

    lv_obj_t *gear = make_btn(scr_main, 100, 32, COL_CYAN, goto_settings_cb);
    lv_obj_align(gear, LV_ALIGN_TOP_RIGHT, -8, 4);
    lv_obj_t *gl = make_label(gear, LV_SYMBOL_SETTINGS, COL_CYAN);
    lv_obj_center(gl);

    /* waveform scope */
    wave_chart = lv_chart_create(scr_main);
    lv_obj_set_style_radius(wave_chart, 0, 0);
    lv_obj_set_size(wave_chart, 290, 110);
    lv_obj_align(wave_chart, LV_ALIGN_TOP_LEFT, 12, 44);
    lv_chart_set_type(wave_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(wave_chart, 120);
    lv_chart_set_range(wave_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_update_mode(wave_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_color(wave_chart, COL_PANEL, 0);
    lv_obj_set_style_border_color(wave_chart, COL_DIM, 0);
    lv_obj_set_style_size(wave_chart, 0, 0, LV_PART_INDICATOR);
    wave_ser = lv_chart_add_series(wave_chart, COL_CYAN, LV_CHART_AXIS_PRIMARY_Y);

    /* notched outline around scope */
    static lv_point_precise_t outline_pts[] = {
        {0, 12}, {12, 0}, {298, 0}, {298, 116}, {0, 116}, {0, 12}
    };
    lv_obj_t *outline = lv_line_create(scr_main);
    lv_line_set_points(outline, outline_pts, 6);
    lv_obj_set_style_line_width(outline, 2, 0);
    lv_obj_set_style_line_color(outline, COL_GREY, 0);
    lv_obj_set_pos(outline, 9, 41);

    /* segmented meter to the right of scope */
    make_seg_meter(&main_meter, scr_main, -8, 156, LV_ALIGN_TOP_RIGHT, 28, 8);

    /* console panel row: fake lights (left) + knob (right) */
    make_fake_cluster(scr_main, 0, 12, 178);
    make_fake_knob(scr_main, 0, 190, 178);
    lv_timer_create(fake_lights_timer_cb, 100, NULL);
    lv_timer_create(fake_knob_timer_cb, 100, NULL);

    /* bottom bar */
    lv_obj_t *lockb = make_btn(scr_main, 100, 34, COL_RED, lock_btn_cb);
    lv_obj_align(lockb, LV_ALIGN_BOTTOM_LEFT, 8, -6);
    lv_obj_t *lk = make_label(lockb, "LOCK", COL_RED);
    lv_obj_center(lk);

    lv_obj_t *mute = make_btn(scr_main, 100, 34, FAKE_BLUE, mute_btn_cb);
    lv_obj_align(mute, LV_ALIGN_BOTTOM_RIGHT, -8, -6);
    mute_btn_lbl = make_label(mute, "LIVE", FAKE_BLUE);
    lv_obj_center(mute_btn_lbl);

    /* TX + CLIP indicators centered in bottom bar */
    lv_obj_t *tx_l = make_label(scr_main, "TX", COL_DIM);
    lv_obj_align(tx_l, LV_ALIGN_BOTTOM_MID, -30, -16);
    tx_lamp = lv_obj_create(scr_main);
    lv_obj_set_size(tx_lamp, 18, 18);
    lv_obj_set_style_radius(tx_lamp, 0, 0);
    lv_obj_set_style_border_width(tx_lamp, 1, 0);
    lv_obj_set_style_border_color(tx_lamp, COL_DIM, 0);
    lv_obj_set_style_bg_color(tx_lamp, COL_PANEL, 0);
    lv_obj_clear_flag(tx_lamp, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(tx_lamp, LV_ALIGN_BOTTOM_MID, -10, -14);

    lv_obj_t *cl_l = make_label(scr_main, "CLIP", COL_DIM);
    lv_obj_align(cl_l, LV_ALIGN_BOTTOM_MID, 18, -16);
    clip_lamp = lv_obj_create(scr_main);
    lv_obj_set_size(clip_lamp, 18, 18);
    lv_obj_set_style_radius(clip_lamp, 0, 0);
    lv_obj_set_style_border_width(clip_lamp, 1, 0);
    lv_obj_set_style_border_color(clip_lamp, COL_DIM, 0);
    lv_obj_set_style_bg_color(clip_lamp, COL_PANEL, 0);
    lv_obj_clear_flag(clip_lamp, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(clip_lamp, LV_ALIGN_BOTTOM_MID, 52, -14);

    lv_timer_create(main_timer_cb, 40, NULL);
}

/* ---------- settings carousel ---------- */
#define NAV_PAGES 4
static lv_obj_t **nav_order[NAV_PAGES] = {&scr_settings, &scr_audio,
                                          &scr_callsign, &scr_bright};
static void rebuild_rollers(const char *text);

static void nav_btn_cb(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *cur = lv_scr_act();
    int idx = 0;
    for (int i = 0; i < NAV_PAGES; i++)
        if (*nav_order[i] == cur) { idx = i; break; }
    int target = idx + dir;
    if (target < 0 || target >= NAV_PAGES) {
        lv_label_set_text(main_callsign_lbl, g_settings.callsign);
        lv_scr_load(scr_main);
        return;
    }
    if (*nav_order[target] == scr_callsign)
        rebuild_rollers(g_settings.callsign);
    lv_scr_load(*nav_order[target]);
}

static void make_nav_bar(lv_obj_t *page, const char *title)
{
    lv_obj_t *left = make_btn(page, 52, 34, COL_CYAN, nav_btn_cb);
    lv_obj_align(left, LV_ALIGN_TOP_LEFT, 8, 4);
    lv_obj_remove_event_cb(left, nav_btn_cb);
    lv_obj_add_event_cb(left, nav_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    lv_obj_t *ll = make_label(left, LV_SYMBOL_LEFT, COL_CYAN);
    lv_obj_center(ll);

    lv_obj_t *right = make_btn(page, 52, 34, COL_CYAN, nav_btn_cb);
    lv_obj_align(right, LV_ALIGN_TOP_RIGHT, -8, 4);
    lv_obj_remove_event_cb(right, nav_btn_cb);
    lv_obj_add_event_cb(right, nav_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_t *rl = make_label(right, LV_SYMBOL_RIGHT, COL_CYAN);
    lv_obj_center(rl);

    lv_obj_t *t = make_label(page, title, COL_RED);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);
}

/* ---------- audio levels screen ---------- */

static void build_audio(void)
{
    scr_audio = make_screen();
    make_nav_bar(scr_audio, "AUDIO");

    lv_obj_t *mic_l = make_label(scr_audio, "MIC", COL_DIM);
    lv_obj_align(mic_l, LV_ALIGN_TOP_LEFT, 8, 54);
    lv_obj_t *mic_sl = lv_slider_create(scr_audio);
    lv_obj_set_size(mic_sl, 290, 14);
    lv_obj_align(mic_sl, LV_ALIGN_TOP_LEFT, 60, 54);
    slider_square(mic_sl);
    lv_slider_set_range(mic_sl, 0, 300);
    lv_slider_set_value(mic_sl, g_settings.gain_in, LV_ANIM_OFF);
    lv_obj_add_event_cb(mic_sl, slider_evt, LV_EVENT_VALUE_CHANGED, (void *)gain_in_cb);

    lv_obj_t *out_l = make_label(scr_audio, "OUT", COL_DIM);
    lv_obj_align(out_l, LV_ALIGN_TOP_LEFT, 8, 86);
    lv_obj_t *out_sl = lv_slider_create(scr_audio);
    lv_obj_set_size(out_sl, 290, 14);
    lv_obj_align(out_sl, LV_ALIGN_TOP_LEFT, 60, 86);
    slider_square(out_sl);
    lv_slider_set_range(out_sl, 0, 300);
    lv_slider_set_value(out_sl, g_settings.gain_out, LV_ANIM_OFF);
    lv_obj_add_event_cb(out_sl, slider_evt, LV_EVENT_VALUE_CHANGED, (void *)gain_out_cb);

    /* voice graph: large area to fill the portrait screen */
    audio_chart = lv_chart_create(scr_audio);
    lv_obj_set_style_radius(audio_chart, 0, 0);
    lv_obj_set_size(audio_chart, 348, 280);
    lv_obj_align(audio_chart, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_chart_set_type(audio_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(audio_chart, 120);
    lv_chart_set_range(audio_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_update_mode(audio_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_color(audio_chart, COL_PANEL, 0);
    lv_obj_set_style_border_color(audio_chart, COL_DIM, 0);
    lv_obj_set_style_size(audio_chart, 0, 0, LV_PART_INDICATOR);
    audio_ser = lv_chart_add_series(audio_chart, COL_CYAN, LV_CHART_AXIS_PRIMARY_Y);
}

/* ---------- settings screen ---------- */

static void set_vox(int v)    { g_settings.vox_thresh = v;   settings_save(); }
static void set_fx(int v)     { g_settings.fx_intensity = v; settings_save(); }
static void set_static(int v) { g_settings.static_lvl = v;   settings_save(); }
static void set_click(int v)  { g_settings.click_vol = v;    settings_save(); }
static void set_bright(int v) { g_settings.brightness = v;   bsp_display_brightness_set(v); settings_save(); }

static void build_settings(void)
{
    scr_settings = make_screen();
    make_nav_bar(scr_settings, "SETTINGS");

    lv_obj_t *list = lv_obj_create(scr_settings);
    lv_obj_set_size(list, 352, 360);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_opa(list, LV_OPA_0, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    make_slider_row(list, "VOX",    0, 100, g_settings.vox_thresh,   set_vox);
    make_slider_row(list, "EFFECT", 0, 100, g_settings.fx_intensity, set_fx);
    make_slider_row(list, "STATIC", 0, 100, g_settings.static_lvl,   set_static);
    make_slider_row(list, "CLICK",  0, 100, g_settings.click_vol,    set_click);
}

/* ---------- brightness page ---------- */
static lv_obj_t *bright_val_lbl;
static void set_bright_page(int v)
{
    if (v < 10) v = 10;
    set_bright(v);
    lv_label_set_text_fmt(bright_val_lbl, "%d%%", v);
}

static void build_bright(void)
{
    scr_bright = make_screen();
    make_nav_bar(scr_bright, "BRIGHTNESS");

    bright_val_lbl = make_label(scr_bright, "", COL_ORANGE);
    lv_obj_set_style_text_font(bright_val_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(bright_val_lbl, LV_ALIGN_CENTER, 0, -20);
    lv_label_set_text_fmt(bright_val_lbl, "%d%%", g_settings.brightness);

    lv_obj_t *sl = lv_slider_create(scr_bright);
    lv_obj_set_size(sl, 320, 20);
    lv_obj_align(sl, LV_ALIGN_CENTER, 0, 30);
    slider_square(sl);
    lv_slider_set_range(sl, 10, 100);
    lv_slider_set_value(sl, g_settings.brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(sl, slider_evt, LV_EVENT_VALUE_CHANGED, (void *)set_bright_page);
}

/* ---------- callsign screen ---------- */

#define ROLLER_OPTS "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL\nM\nN\nO\nP\nQ\nR\nS\nT\nU\nV\nW\nX\nY\nZ\n0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n-"
static const char roller_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
static lv_obj_t *rollers[CALLSIGN_MAX];
static int roller_count = 0;
static lv_obj_t *roller_cont;

static int char_to_idx(char c)
{
    const char *p = strchr(roller_chars, c);
    return p ? (int)(p - roller_chars) : 0;
}

static void rebuild_rollers(const char *text)
{
    lv_obj_clean(roller_cont);
    roller_count = strlen(text);
    if (roller_count < 1) roller_count = 1;
    if (roller_count > CALLSIGN_MAX) roller_count = CALLSIGN_MAX;
    for (int i = 0; i < roller_count; i++) {
        lv_obj_t *r = lv_roller_create(roller_cont);
        lv_roller_set_options(r, ROLLER_OPTS, LV_ROLLER_MODE_INFINITE);
        lv_roller_set_visible_row_count(r, 4);
        lv_obj_set_width(r, 32);
        lv_obj_set_style_bg_color(r, COL_PANEL, 0);
        lv_obj_set_style_text_color(r, COL_DIM, 0);
        lv_obj_set_style_bg_color(r, COL_RED, LV_PART_SELECTED);
        lv_obj_set_style_text_color(r, lv_color_white(), LV_PART_SELECTED);
        char c = (i < (int)strlen(text)) ? text[i] : 'A';
        lv_roller_set_selected(r, char_to_idx(c), LV_ANIM_OFF);
        rollers[i] = r;
    }
}

static void cs_plus_cb(lv_event_t *e)
{
    if (roller_count < CALLSIGN_MAX) {
        char buf[CALLSIGN_MAX + 1];
        for (int i = 0; i < roller_count; i++)
            buf[i] = roller_chars[lv_roller_get_selected(rollers[i])];
        buf[roller_count] = 'A';
        buf[roller_count + 1] = '\0';
        rebuild_rollers(buf);
    }
}
static void cs_minus_cb(lv_event_t *e)
{
    if (roller_count > 1) {
        char buf[CALLSIGN_MAX + 1];
        for (int i = 0; i < roller_count - 1; i++)
            buf[i] = roller_chars[lv_roller_get_selected(rollers[i])];
        buf[roller_count - 1] = '\0';
        rebuild_rollers(buf);
    }
}
static void cs_commit_cb(lv_event_t *e)
{
    for (int i = 0; i < roller_count; i++)
        g_settings.callsign[i] = roller_chars[lv_roller_get_selected(rollers[i])];
    g_settings.callsign[roller_count] = '\0';
    settings_save();
}

static void build_callsign(void)
{
    scr_callsign = make_screen();
    make_nav_bar(scr_callsign, "CALLSIGN");

    roller_cont = lv_obj_create(scr_callsign);
    lv_obj_set_size(roller_cont, 352, 280);
    lv_obj_align(roller_cont, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_opa(roller_cont, LV_OPA_0, 0);
    lv_obj_set_style_border_width(roller_cont, 0, 0);
    lv_obj_set_style_pad_all(roller_cont, 0, 0);
    lv_obj_set_style_pad_column(roller_cont, 4, 0);
    lv_obj_set_flex_flow(roller_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(roller_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(roller_cont, LV_OBJ_FLAG_SCROLLABLE);

    struct { const char *txt; lv_event_cb_t cb; lv_color_t col; } btns[] = {
        {LV_SYMBOL_MINUS, cs_minus_cb, COL_CYAN},
        {LV_SYMBOL_PLUS,  cs_plus_cb,  COL_CYAN},
        {"COMMIT",        cs_commit_cb, COL_GREEN},
    };
    int x = 36;
    for (int i = 0; i < 3; i++) {
        int w = (i < 2) ? 80 : 130;
        lv_obj_t *b = make_btn(scr_callsign, w, 36, btns[i].col, btns[i].cb);
        lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, x, -8);
        lv_obj_t *l = make_label(b, btns[i].txt, btns[i].col);
        lv_obj_center(l);
        x += w + 14;
    }
}

/* ---------- lock screen ---------- */

static void lock_tap_cb(lv_event_t *e)
{
    uint32_t now = lv_tick_get();
    s_tap_times[s_tap_idx % UNLOCK_TAPS] = now;
    s_tap_idx++;
    if (s_tap_idx >= UNLOCK_TAPS) {
        uint32_t oldest = s_tap_times[s_tap_idx % UNLOCK_TAPS];
        if (now - oldest <= UNLOCK_WINDOW_MS) {
            s_tap_idx = 0;
            lv_scr_load(scr_main);
            return;
        }
    }
    lv_obj_set_style_text_color(lock_hex, COL_RED, 0);
}

static void lock_timer_cb(lv_timer_t *t)
{
    if (lv_scr_act() != scr_lock) return;
    static uint32_t x = 0x1B7A;
    x = x * 1664525u + 1013904223u;
    lv_label_set_text_fmt(lock_hex, "%04X-%04X", (unsigned)(x >> 16) & 0xFFFF,
                          (unsigned)x & 0xFFFF);
    lv_obj_set_style_text_color(lock_hex, COL_ORANGE, 0);
    for (int i = 0; i < 5; i++) {
        int h = 6 + ((x >> (i * 5)) & 0x0F);
        lv_obj_set_height(lock_bars[i], h);
        lv_obj_align(lock_bars[i], LV_ALIGN_BOTTOM_LEFT, 8 + i * 16, -2);
    }
}

static void build_lock(void)
{
    scr_lock = make_screen();
    lv_obj_add_flag(scr_lock, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr_lock, lock_tap_cb, LV_EVENT_CLICKED, NULL);

    aurebesh_28 = lv_tiny_ttf_create_data(aurebesh_ttf_start,
                                          aurebesh_ttf_end - aurebesh_ttf_start, 28);
    aurebesh_16 = lv_tiny_ttf_create_data(aurebesh_ttf_start,
                                          aurebesh_ttf_end - aurebesh_ttf_start, 16);

    /* top bar - mirrored from main */
    lock_callsign = make_label(scr_lock, g_settings.callsign, COL_RED);
    if (callsign_font_28) lv_obj_set_style_text_font(lock_callsign, callsign_font_28, 0);
    lv_obj_align(lock_callsign, LV_ALIGN_TOP_LEFT, 10, 6);

    lv_obj_t *hexbox = lv_obj_create(scr_lock);
    lv_obj_set_size(hexbox, 100, 32);
    lv_obj_set_style_radius(hexbox, 0, 0);
    lv_obj_set_style_bg_color(hexbox, COL_PANEL, 0);
    lv_obj_set_style_border_width(hexbox, 1, 0);
    lv_obj_set_style_border_color(hexbox, COL_CYAN, 0);
    lv_obj_clear_flag(hexbox, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(hexbox, LV_ALIGN_TOP_RIGHT, -8, 4);
    lock_hex = make_label(hexbox, "0000-0000", COL_ORANGE);
    lv_obj_center(lock_hex);

    /* scope - red trace */
    lock_wave = lv_chart_create(scr_lock);
    lv_obj_set_style_radius(lock_wave, 0, 0);
    lv_obj_set_size(lock_wave, 290, 110);
    lv_obj_align(lock_wave, LV_ALIGN_TOP_LEFT, 12, 44);
    lv_chart_set_type(lock_wave, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(lock_wave, 120);
    lv_chart_set_range(lock_wave, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_update_mode(lock_wave, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_color(lock_wave, COL_PANEL, 0);
    lv_obj_set_style_border_color(lock_wave, COL_DIM, 0);
    lv_obj_set_style_size(lock_wave, 0, 0, LV_PART_INDICATOR);
    lv_obj_clear_flag(lock_wave, LV_OBJ_FLAG_CLICKABLE);
    lock_ser = lv_chart_add_series(lock_wave, COL_RED, LV_CHART_AXIS_PRIMARY_Y);

    static lv_point_precise_t lock_outline_pts[] = {
        {0, 12}, {12, 0}, {298, 0}, {298, 116}, {0, 116}, {0, 12}
    };
    lv_obj_t *outline = lv_line_create(scr_lock);
    lv_line_set_points(outline, lock_outline_pts, 6);
    lv_obj_set_style_line_width(outline, 2, 0);
    lv_obj_set_style_line_color(outline, COL_GREY, 0);
    lv_obj_set_pos(outline, 9, 41);
    lv_obj_clear_flag(outline, LV_OBJ_FLAG_CLICKABLE);

    /* console panel row */
    make_fake_cluster(scr_lock, 1, 12, 178);
    make_fake_knob(scr_lock, 1, 190, 178);
    make_seg_meter(&lock_meter, scr_lock, -8, 156, LV_ALIGN_TOP_RIGHT, 28, 8);

    /* bottom bar - locked variants */
    lv_obj_t *lockbox = lv_obj_create(scr_lock);
    lv_obj_set_size(lockbox, 100, 34);
    lv_obj_set_style_radius(lockbox, 0, 0);
    lv_obj_set_style_bg_color(lockbox, COL_PANEL, 0);
    lv_obj_set_style_border_width(lockbox, 1, 0);
    lv_obj_set_style_border_color(lockbox, COL_RED, 0);
    lv_obj_clear_flag(lockbox, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(lockbox, LV_ALIGN_BOTTOM_LEFT, 8, -6);
    lv_obj_t *lockl = make_label(lockbox, "LOCKED", COL_RED);
    if (aurebesh_16) lv_obj_set_style_text_font(lockl, aurebesh_16, 0);
    lv_obj_center(lockl);

    /* TX + CLIP */
    lv_obj_t *tx_l = make_label(scr_lock, "TX", COL_DIM);
    if (aurebesh_16) lv_obj_set_style_text_font(tx_l, aurebesh_16, 0);
    lv_obj_align(tx_l, LV_ALIGN_BOTTOM_MID, -30, -16);
    lock_tx_lamp = lv_obj_create(scr_lock);
    lv_obj_set_size(lock_tx_lamp, 18, 18);
    lv_obj_set_style_radius(lock_tx_lamp, 0, 0);
    lv_obj_set_style_border_width(lock_tx_lamp, 1, 0);
    lv_obj_set_style_border_color(lock_tx_lamp, COL_DIM, 0);
    lv_obj_set_style_bg_color(lock_tx_lamp, COL_PANEL, 0);
    lv_obj_clear_flag(lock_tx_lamp, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(lock_tx_lamp, LV_ALIGN_BOTTOM_MID, -10, -14);

    lv_obj_t *cl_l = make_label(scr_lock, "CLIP", COL_DIM);
    if (aurebesh_16) lv_obj_set_style_text_font(cl_l, aurebesh_16, 0);
    lv_obj_align(cl_l, LV_ALIGN_BOTTOM_MID, 18, -16);
    lock_clip_lamp = lv_obj_create(scr_lock);
    lv_obj_set_size(lock_clip_lamp, 18, 18);
    lv_obj_set_style_radius(lock_clip_lamp, 0, 0);
    lv_obj_set_style_border_width(lock_clip_lamp, 1, 0);
    lv_obj_set_style_border_color(lock_clip_lamp, COL_DIM, 0);
    lv_obj_set_style_bg_color(lock_clip_lamp, COL_PANEL, 0);
    lv_obj_clear_flag(lock_clip_lamp, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(lock_clip_lamp, LV_ALIGN_BOTTOM_MID, 52, -14);

    /* signal bars box (bottom right) */
    lv_obj_t *barbox = lv_obj_create(scr_lock);
    lv_obj_set_size(barbox, 100, 34);
    lv_obj_set_style_radius(barbox, 0, 0);
    lv_obj_set_style_bg_color(barbox, COL_PANEL, 0);
    lv_obj_set_style_border_width(barbox, 1, 0);
    lv_obj_set_style_border_color(barbox, FAKE_BLUE, 0);
    lv_obj_set_style_pad_all(barbox, 2, 0);
    lv_obj_clear_flag(barbox, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(barbox, LV_ALIGN_BOTTOM_RIGHT, -8, -6);
    for (int i = 0; i < 5; i++) {
        lock_bars[i] = lv_obj_create(barbox);
        lv_obj_set_size(lock_bars[i], 10, 12);
        lv_obj_set_style_bg_color(lock_bars[i], COL_GREEN, 0);
        lv_obj_set_style_border_width(lock_bars[i], 0, 0);
        lv_obj_set_style_radius(lock_bars[i], 0, 0);
        lv_obj_clear_flag(lock_bars[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(lock_bars[i], LV_ALIGN_BOTTOM_LEFT, 8 + i * 16, -2);
    }

    lv_timer_create(lock_timer_cb, 350, NULL);
}

/* ---------- public API ---------- */

void ui_request_factory_reset(void)
{
    if (!bsp_display_lock(100)) return;
    show_confirm("FACTORY RESET", "All settings and callsign will be erased.",
                 "RESET", settings_factory_reset);
    bsp_display_unlock();
}

void ui_request_mute_toggle(void)
{
    if (!bsp_display_lock(100)) return;
    mute_toggle();
    bsp_display_unlock();
}

void ui_request_lock(void)
{
    if (!bsp_display_lock(100)) return;
    if (lv_scr_act() != scr_lock) {
        s_tap_idx = 0;
        lv_label_set_text(lock_callsign, g_settings.callsign);
        lv_scr_load(scr_lock);
    }
    bsp_display_unlock();
}

void ui_init(void)
{
    audio_set_gain_in(g_settings.gain_in);
    audio_set_gain_out(g_settings.gain_out);
    callsign_font_20 = lv_tiny_ttf_create_data(aureng_ttf_start,
                                               aureng_ttf_end - aureng_ttf_start, 20);
    callsign_font_28 = lv_tiny_ttf_create_data(aureng_ttf_start,
                                               aureng_ttf_end - aureng_ttf_start, 28);
    build_main();
    build_audio();
    build_settings();
    build_bright();
    build_callsign();
    build_lock();
    lv_scr_load(scr_main);
}
