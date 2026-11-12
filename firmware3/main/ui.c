/* Stormtrooper Voice Changer UI - firmware3 (LVGL v8.4, raw SH8601 path).
 * Built up incrementally from a verified blank screen. Each step is flashed
 * and visually confirmed before the next is added.
 *   step 1: top bar (callsign + gear)
 *   step 2: waveform scope + 40ms timer
 *   step 3: console row (fake lights + knob) + seg meter + bottom bar */
#include <math.h>
#include "esp_random.h"
#include "lvgl.h"
#include "ui.h"
#include "audio.h"

/* ---- palette (from the original 1.47 design) ---- */
#define COL_BG     lv_color_hex(0x000000)
#define COL_PANEL  lv_color_hex(0x0C0F12)
#define COL_RED    lv_color_hex(0x9B1B1B)
#define COL_CYAN   lv_color_hex(0x3A6E96)
#define COL_DIM    lv_color_hex(0x6E6E6E)
#define COL_GREEN  lv_color_hex(0x3C7A3C)
#define COL_GREY   lv_color_hex(0x8A8A8A)
#define COL_ORANGE lv_color_hex(0xEEB52A)
#define COL_WHITE  lv_color_hex(0xE8E8E8)

#define FAKE_GREY   lv_color_hex(0x979B9A)
#define FAKE_RED    lv_color_hex(0xD00A1B)
#define FAKE_BLUE   lv_color_hex(0x459FC2)
#define FAKE_ORANGE lv_color_hex(0xBF5B16)

/* Rounded display: corners are physically clipped (~45px radius). Keep content
 * out of the corners - inset end items of the top/bottom bars by CORNER_SAFE. */
#define EDGE_SAFE   14
#define CORNER_SAFE 44

static lv_obj_t *scr_main;
static lv_obj_t *wave_chart;
static lv_chart_series_t *wave_ser;
static lv_obj_t *live_lbl;

/* ---- fake console lights ---- */
#define FAKE_LIGHTS 7
static lv_obj_t *fake_lights[FAKE_LIGHTS];
static uint8_t   fake_state[FAKE_LIGHTS];
static uint16_t  fake_next[FAKE_LIGHTS];
static uint32_t  fake_rng = 0xDEAD17;

/* ---- segmented level meter ---- */
#define METER_SEGS 12
#define METER_RED_FROM 9
typedef struct { lv_obj_t *seg[METER_SEGS]; } seg_meter_t;
static seg_meter_t main_meter;

/* ---- helpers ---- */
static lv_obj_t *make_label(lv_obj_t *parent, const char *txt, lv_color_t col)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, col, 0);
    return l;
}

static lv_obj_t *make_btn(lv_obj_t *parent, int w, int h, lv_color_t border)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_bg_color(b, COL_PANEL, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, border, 0);
    return b;
}

static lv_color_t fake_state_color(uint8_t st)
{
    switch (st) {
    case 1: return FAKE_ORANGE;
    case 2: return FAKE_RED;
    case 3: return FAKE_BLUE;
    default: return FAKE_GREY;
    }
}

static uint16_t fake_next_delay(void)
{
    fake_rng = fake_rng * 1664525u + 1013904223u;
    return 200 + ((fake_rng >> 8) % 201);
}

static void fake_lights_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    static int tick = 0;
    tick++;
    for (int i = 0; i < FAKE_LIGHTS; i++) {
        if (!fake_lights[i]) continue;
        if (--fake_next[i] == 0) {
            fake_rng = fake_rng * 1664525u + 1013904223u;
            fake_state[i] = (fake_state[i] + 1 + ((fake_rng >> 8) % 3)) % 4;
            fake_next[i] = fake_next_delay();
        }
        lv_obj_set_style_bg_color(fake_lights[i], fake_state_color(fake_state[i]), 0);
        if (fake_state[i] == 2) {
            int ph = (tick + i * 8) % 60;
            int tri = ph < 30 ? ph : 60 - ph;
            lv_obj_set_style_bg_opa(fake_lights[i], 120 + tri * 4, 0);
        } else {
            lv_obj_set_style_bg_opa(fake_lights[i], LV_OPA_COVER, 0);
        }
    }
}

static void make_fake_cluster(lv_obj_t *parent, int x_base, int y_base)
{
    static const struct { int x, y, w, h; bool dome; } fl[FAKE_LIGHTS] = {
        {0,  0,  80, 14, false},
        {0,  22, 32, 14, false}, {42, 22, 28, 28, true},
        {0,  44, 32, 14, false}, {42, 58, 32, 14, false},
        {0,  66, 28, 28, true},  {42, 80, 32, 14, false},
    };
    for (int i = 0; i < FAKE_LIGHTS; i++) {
        lv_obj_t *f = lv_obj_create(parent);
        lv_obj_set_size(f, fl[i].w, fl[i].h);
        lv_obj_set_style_radius(f, fl[i].dome ? LV_RADIUS_CIRCLE : 0, 0);
        lv_obj_set_style_border_width(f, 0, 0);
        lv_obj_clear_flag(f, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(f, LV_ALIGN_TOP_LEFT, x_base + fl[i].x, y_base + fl[i].y);
        fake_state[i] = esp_random() % 4;
        lv_obj_set_style_bg_color(f, fake_state_color(fake_state[i]), 0);
        fake_next[i] = fake_next_delay();
        fake_lights[i] = f;
    }
}

/* ---- rotating knob (two concentric arc groups) ---- */
static lv_obj_t *knob_outer[3], *knob_inner;
static int knob_outer_rot = 0, knob_inner_rot = 90;
static int knob_outer_spd = 1, knob_inner_spd = -2;

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
    LV_UNUSED(t);
    if (!knob_inner) return;
    fake_rng = fake_rng * 1664525u + 1013904223u;
    if ((fake_rng & 0x7F) == 0) {
        int v = 1 + (int)((fake_rng >> 8) % 3);
        knob_outer_spd = (fake_rng & 0x100) ? v : -v;
    }
    if ((fake_rng & 0x7F) == 1) {
        int v = 1 + (int)((fake_rng >> 16) % 3);
        knob_inner_spd = (fake_rng & 0x100) ? v : -v;
    }
    knob_outer_rot = (knob_outer_rot + knob_outer_spd + 360) % 360;
    knob_inner_rot = (knob_inner_rot + knob_inner_spd + 360) % 360;
    for (int i = 0; i < 3; i++)
        lv_arc_set_rotation(knob_outer[i], knob_outer_rot + i * 120);
    lv_arc_set_rotation(knob_inner, knob_inner_rot);
}

static void make_fake_knob(lv_obj_t *parent, int x, int y)
{
    for (int i = 0; i < 3; i++)
        knob_outer[i] = make_fake_arc(parent, 80, x, y, 0, 80, 8, COL_GREY);
    knob_inner = make_fake_arc(parent, 44, x + 18, y + 18, 0, 250, 7, COL_DIM);

    lv_obj_t *accent = lv_obj_create(parent);
    lv_obj_set_size(accent, 80, 12);
    lv_obj_set_style_radius(accent, 0, 0);
    lv_obj_set_style_bg_color(accent, COL_DIM, 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(accent, LV_ALIGN_TOP_LEFT, x, y + 86);
}

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

static void meter_set(seg_meter_t *m, int level /*0..100*/)
{
    int lit = level * METER_SEGS / 100;
    for (int i = 0; i < METER_SEGS; i++) {
        lv_color_t c = COL_PANEL;
        if (i < lit) c = (i >= METER_RED_FROM) ? COL_RED : COL_GREEN;
        lv_obj_set_style_bg_color(m->seg[i], c, 0);
    }
}

/* Feed the scope + meter from the live mic level. Runs in the LVGL task
 * under its lock, so no extra locking needed. */
static void scope_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    int lvl = audio_get_level();            /* 0..100 peak of last block */
    lv_chart_set_next_value(wave_chart, wave_ser, lvl);
    meter_set(&main_meter, lvl);

    if (live_lbl) {
        bool m = audio_is_muted();
        lv_label_set_text(live_lbl, m ? "MUTED" : "LIVE");
        lv_obj_set_style_text_color(live_lbl, m ? COL_RED : COL_CYAN, 0);
    }
}

void ui_init(void)
{
    scr_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_main, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr_main, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr_main, LV_OBJ_FLAG_SCROLLABLE);

    /* top bar - inset from the clipped corners */
    lv_obj_t *cs = make_label(scr_main, "TK-421", COL_RED);
    lv_obj_align(cs, LV_ALIGN_TOP_LEFT, CORNER_SAFE, 18);

    lv_obj_t *gear = make_btn(scr_main, 40, 30, COL_DIM);
    lv_obj_align(gear, LV_ALIGN_TOP_RIGHT, -CORNER_SAFE, 14);
    lv_obj_t *gl = make_label(gear, LV_SYMBOL_SETTINGS, COL_CYAN);
    lv_obj_center(gl);

    /* waveform scope */
    wave_chart = lv_chart_create(scr_main);
    lv_obj_set_style_radius(wave_chart, 0, 0);
    lv_obj_set_size(wave_chart, 268, 110);
    lv_obj_align(wave_chart, LV_ALIGN_TOP_LEFT, 12, 60);
    lv_chart_set_type(wave_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(wave_chart, 120);
    lv_chart_set_range(wave_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_update_mode(wave_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_color(wave_chart, COL_PANEL, 0);
    lv_obj_set_style_border_color(wave_chart, COL_DIM, 0);
    lv_obj_set_style_width(wave_chart, 0, LV_PART_INDICATOR);   /* hide points */
    lv_obj_set_style_height(wave_chart, 0, LV_PART_INDICATOR);
    wave_ser = lv_chart_add_series(wave_chart, COL_CYAN, LV_CHART_AXIS_PRIMARY_Y);

    /* notched outline around the scope */
    static lv_point_t outline_pts[] = {
        {0, 12}, {12, 0}, {276, 0}, {276, 116}, {0, 116}, {0, 12}
    };
    lv_obj_t *outline = lv_line_create(scr_main);
    lv_line_set_points(outline, outline_pts, 6);
    lv_obj_set_style_line_width(outline, 2, 0);
    lv_obj_set_style_line_color(outline, COL_GREY, 0);
    lv_obj_set_pos(outline, 9, 57);

    /* segmented meter to the right of the scope */
    make_seg_meter(&main_meter, scr_main, -EDGE_SAFE, 168, LV_ALIGN_TOP_RIGHT, 22, 8);

    /* console row: fake lights (left) + knob (right) */
    make_fake_cluster(scr_main, 28, 196);
    make_fake_knob(scr_main, 210, 196);

    /* bottom bar - inset from clipped bottom corners */
    lv_obj_t *lockb = make_btn(scr_main, 96, 34, COL_RED);
    lv_obj_align(lockb, LV_ALIGN_BOTTOM_LEFT, CORNER_SAFE - 12, -42);
    lv_obj_center(make_label(lockb, "LOCK", COL_RED));

    lv_obj_t *live = make_btn(scr_main, 96, 34, COL_CYAN);
    lv_obj_align(live, LV_ALIGN_BOTTOM_RIGHT, -(CORNER_SAFE - 12), -42);
    live_lbl = make_label(live, "MUTED", COL_RED);
    lv_obj_center(live_lbl);

    lv_obj_t *tx = make_label(scr_main, "TX", COL_DIM);
    lv_obj_align(tx, LV_ALIGN_BOTTOM_MID, -16, -52);
    lv_obj_t *cl = make_label(scr_main, "CLIP", COL_DIM);
    lv_obj_align(cl, LV_ALIGN_BOTTOM_MID, 22, -52);

    /* timers */
    lv_timer_create(scope_timer_cb, 40, NULL);
    lv_timer_create(fake_lights_timer_cb, 100, NULL);
    lv_timer_create(fake_knob_timer_cb, 100, NULL);

    lv_scr_load(scr_main);
}
