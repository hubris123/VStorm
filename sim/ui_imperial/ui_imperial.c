/* Imperial console UI for the ESP32-S3-Touch-AMOLED-1.8 (368 x 448), LVGL 8.4.
 *
 * Layers (docs/UI-DESIGN.md section 6b):
 *   background  one RGB565 image per screen = the skin (design/skins/default/ (PNG)):
 *               key housings, cap wells, frames, grids, fixed labels
 *   live        LVGL objects on top: key caps, lamps, scope, meter, Scomp rings,
 *               readouts, values. Coordinates match design/mockups/make_mockups.py.
 *
 * Screens: MAIN, SETTINGS (8 pages), CALLSIGN (code cylinder), LOCK (5 taps to unlock).
 * Reacts to the voice through the VOX gate (section 6a). */
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "esp_random.h"
#include "audio.h"
#include "ui.h"
#include "ui_imperial.h"

/* ---------------- palette (docs/UI-DESIGN.md section 2) ---------------- */
#define C(hex) lv_color_hex(hex)
#define COL_PANEL  C(0x0C0F12)
#define COL_FRAME  C(0x5C6166)
#define COL_TEXT   C(0xC9CDD0)
#define COL_LABEL  C(0xA9B0B5)
#define COL_RED    C(0xC4262E)
#define COL_GREEN  C(0x3C8A44)
#define COL_BLUE   C(0x459FC2)
#define COL_AMBER  C(0xE0A526)
#define COL_DIM    C(0x3A3F44)
#define COL_GREY   C(0x8A8A8A)
#define COL_TICK   C(0x916B19)     /* amber at 65 %: readout microtext */
static const uint32_t LAMP_HEX[4] = {0x979B9A, 0xBF5B16, 0xD00A1B, 0x459FC2}; /* grey orange red blue */

#define SCR_W 368
#define SCR_H 448

static lv_font_t *f_aureb13, *f_aureb12, *f_aureng26, *f_aureng20, *f_aureng28, *f_aureng42, *f_aureng34, *f_aureng22, *f_aureng18;
static ui_settings_t S;

/* ---------------- helpers ---------------- */
/* xorshift32: every bit is usable (the old LCG's low bits repeated in short patterns,
 * which made "random" ring reversals rare) */
static uint32_t rng_state = 0x5EED1234;
static uint32_t rnd(void)
{
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

static lv_img_dsc_t mk_skin(const uint8_t *px)
{
    lv_img_dsc_t d;
    memset(&d, 0, sizeof(d));
    d.header.cf = LV_IMG_CF_TRUE_COLOR;
    d.header.w = SCR_W;
    d.header.h = SCR_H;
    d.data_size = SCR_W * SCR_H * 2;
    d.data = px;
    return d;
}
static lv_img_dsc_t skin_main, skin_lock, skin_set_value, skin_set_wide, skin_callsign, skin_set_sensors;

static lv_obj_t *make_screen(const lv_img_dsc_t *skin, lv_obj_t **bg_out)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s, lv_color_black(), 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *bg = lv_img_create(s);
    lv_img_set_src(bg, skin);
    lv_obj_set_pos(bg, 0, 0);
    if (bg_out) *bg_out = bg;
    return s;
}

static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, lv_color_t c)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *label(lv_obj_t *p, const char *t, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

/* place a label centred on (cx, cy) */
static void center_at(lv_obj_t *o, int cx, int cy)
{
    lv_obj_update_layout(o);
    lv_obj_set_pos(o, cx - lv_obj_get_width(o) / 2, cy - lv_obj_get_height(o) / 2);
}

static lv_obj_t *aureb_label(lv_obj_t *p, const char *t, lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = label(p, t, f ? f : LV_FONT_DEFAULT, c);
    lv_obj_set_style_text_letter_space(l, 3, 0);   /* spaced so it reads like a script */
    return l;
}

/* ---------------- key caps (the pressable part; housings are in the skin) ---------------- */
typedef struct {
    lv_obj_t *cap, *lbl, *sub;
    lv_color_t accent;
} cap_t;

static void cap_style(cap_t *k, bool lit)
{
    lv_obj_t *c = k->cap;
    if (lit) {
        lv_obj_set_style_bg_color(c, lv_color_mix(lv_color_white(), k->accent, 64), 0);
        lv_obj_set_style_bg_grad_color(c, lv_color_mix(lv_color_black(), k->accent, 64), 0);
        lv_obj_set_style_shadow_color(c, k->accent, 0);
        lv_obj_set_style_shadow_width(c, 22, 0);
        lv_obj_set_style_shadow_spread(c, 2, 0);
        lv_obj_set_style_shadow_opa(c, LV_OPA_70, 0);
        /* yellow caps take dark lettering; red/green take near-white */
        lv_color_t ink = lv_color_to32(k->accent) == lv_color_to32(COL_AMBER) ? C(0x1A1200) : C(0xFFF4F0);
        lv_obj_set_style_text_color(k->lbl, ink, 0);
        if (k->sub) lv_obj_set_style_text_color(k->sub, ink, 0);
    } else {
        lv_obj_set_style_bg_color(c, C(0x353C43), 0);
        lv_obj_set_style_bg_grad_color(c, C(0x1A1E22), 0);
        lv_obj_set_style_shadow_width(c, 0, 0);
        lv_obj_set_style_text_color(k->lbl, k->accent, 0);
        if (k->sub) lv_obj_set_style_text_color(k->sub, COL_LABEL, 0);
    }
}

/* sub-label: 14 px, 12 px only if it doesn't fit the cap (docs/UI-DESIGN.md section 3) */
static void sub_fit(cap_t *k)
{
    if (!k->sub) return;
    lv_point_t sz;
    lv_txt_get_size(&sz, lv_label_get_text(k->sub), &lv_font_montserrat_14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_style_text_font(k->sub, sz.x > lv_obj_get_width(k->cap) - 10 ? &lv_font_montserrat_12
                                                                         : &lv_font_montserrat_14, 0);
}

/* housing at (hx, hy, hw, hh); the cap sits 11 px inside, touch area = whole housing */
static cap_t make_cap(lv_obj_t *scr, int hx, int hy, int hw, int hh, const char *txt, const char *sub,
                      lv_color_t accent, bool symbol, lv_event_cb_t cb, lv_event_code_t code, void *ud)
{
    cap_t k = {0};
    k.accent = accent;
    lv_obj_t *c = lv_obj_create(scr);
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, hx + 11, hy + 11);
    lv_obj_set_size(c, hw - 22, hh - 22);
    lv_obj_set_style_radius(c, 6, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(c, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(c, C(0x050607), 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_translate_y(c, 2, LV_STATE_PRESSED);          /* key travel */
    lv_obj_set_style_bg_color(c, C(0x23292E), LV_STATE_PRESSED);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_ext_click_area(c, 11);
    k.cap = c;
    k.lbl = label(c, txt, symbol ? &lv_font_montserrat_28 : &lv_font_montserrat_18, accent);
    if (sub) {
        k.sub = label(c, sub, &lv_font_montserrat_14, COL_LABEL);
        sub_fit(&k);
        lv_obj_align(k.lbl, LV_ALIGN_CENTER, 0, symbol ? -12 : -10);
        lv_obj_align(k.sub, LV_ALIGN_CENTER, 0, symbol ? 16 : 13);
    } else {
        lv_obj_center(k.lbl);
    }
    cap_style(&k, false);
    if (cb) {
        lv_obj_add_event_cb(c, cb, code, ud);
        if (code == LV_EVENT_PRESSED)   /* hold-to-repeat keys */
            lv_obj_add_event_cb(c, cb, LV_EVENT_LONG_PRESSED_REPEAT, ud);
    }
    return k;
}

/* ================= console widgets shared by MAIN and LOCK ================= */
#define METER_SEGS 12
#define CLUSTER_Y 196     /* lamp cluster 196-304: even gaps to the scope frame and the keys */
#define SCOMP_CY 250      /* Scomp link centred on the cluster */
/* middle row: each item centred over the key below it (key centres x 68 / 184 / 300) */
#define CLUSTER_X 28      /* 80 px wide lamp cluster over LOCK */
#define TICK_CX 184       /* readout lines centred over SYS */
#define SCOMP_CX 300      /* Scomp link over LIVE (socket ring = key width) */
static const int LAMP_ROW[4] = {7, 36, 65, 94};
#define LAMPS 7
#define TICK_LINES 4

static const char *WORDS[] = {"SECTOR", "ACCESS", "SCANNER", "CHANNEL", "UPLINK", "CIPHER", "RELAY",
                              "VECTOR", "SIGNAL", "BEACON", "ORBIT", "PATROL", "DOCKING", "CLEARED",
                              "ARMED", "COMLINK"};
#define NWORDS (sizeof(WORDS) / sizeof(WORDS[0]))

typedef struct {
    lv_obj_t *scr, *callsign, *tx, *clip, *chart, *frame;
    lv_chart_series_t *ser;
    lv_obj_t *meter[METER_SEGS];
    lv_obj_t *lamp[LAMPS];
    uint8_t lamp_st[LAMPS];
    uint16_t lamp_next[LAMPS];
    lv_obj_t *tick[TICK_LINES];
    uint8_t tick_word[TICK_LINES];
    int tick_next;
    lv_obj_t *ring_outer[3], *ring_inner, *ring_tip[2];
    float ang[3], spd[3];
    uint16_t rev_next[3];                 /* ticks until each ring reverses */
    lv_obj_t *batt;                       /* battery bar beside the meter */
    lv_obj_t *tilt_v, *tilt_h;            /* tilt crosshair lines in the scope */
    float tilt_x, tilt_y, tilt_opa;
    lv_obj_t *t_word, *t_num, *t_unit;    /* secret temp: Aurebesh TEMP + TK-style digits + Aurebesh unit */
    float temp_c;
} console_t;

static console_t con_main, con_lock;

/* Callsign in the top bar: as big as fits. 42 px normally; long callsigns step down
 * (34, 26, 22) so they never run into the TX / CLIP lamps (space x 46..222). */
#define CS_MAX_W 176
static void callsign_show(console_t *c)
{
    lv_font_t *opts[4] = {f_aureng42, f_aureng34, f_aureng26, f_aureng22};
    const lv_font_t *f = &lv_font_montserrat_22;
    for (int i = 0; i < 4; i++) {
        if (!opts[i]) continue;
        lv_point_t sz;
        lv_txt_get_size(&sz, S.callsign, opts[i], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        f = opts[i];
        if (sz.x <= CS_MAX_W) break;
    }
    lv_obj_set_style_text_font(c->callsign, f, 0);
    lv_label_set_text(c->callsign, S.callsign);
    lv_obj_update_layout(c->callsign);
    lv_obj_set_pos(c->callsign, 46, 31 - lv_obj_get_height(c->callsign) / 2);   /* centred in the top bar */
}

/* scope interior for the tilt lines */
#define SCOPE_X0 21
#define SCOPE_X1 285
#define SCOPE_Y0 65
#define SCOPE_Y1 173
#define TILT_COL C(0x46525A)      /* a shade whiter than the grid (#1E2429) */
#define TILT_RANGE 45.0f          /* degrees to reach the scope edge */

static void make_chart(console_t *c, lv_color_t trace)
{
    c->tilt_v = box(c->scr, (SCOPE_X0 + SCOPE_X1) / 2, SCOPE_Y0, 1, SCOPE_Y1 - SCOPE_Y0, TILT_COL);
    c->tilt_h = box(c->scr, SCOPE_X0, (SCOPE_Y0 + SCOPE_Y1) / 2, SCOPE_X1 - SCOPE_X0, 1, TILT_COL);
    c->tilt_opa = 255;
    lv_obj_t *ch = lv_chart_create(c->scr);
    lv_obj_set_pos(ch, 19, 63);
    lv_obj_set_size(ch, 269, 113);
    lv_obj_set_style_bg_opa(ch, LV_OPA_0, 0);          /* panel + grid are in the skin */
    lv_obj_set_style_border_width(ch, 0, 0);
    lv_obj_set_style_radius(ch, 0, 0);
    lv_obj_set_style_pad_all(ch, 4, 0);
    lv_chart_set_div_line_count(ch, 0, 0);
    lv_chart_set_type(ch, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(ch, 60);
    lv_chart_set_range(ch, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_update_mode(ch, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_size(ch, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(ch, 2, LV_PART_ITEMS);
    lv_obj_clear_flag(ch, LV_OBJ_FLAG_CLICKABLE);
    c->ser = lv_chart_add_series(ch, trace, LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < 60; i++) lv_chart_set_next_value(ch, c->ser, 3);
    c->chart = ch;

    static lv_point_t pts[] = {{0, 12}, {12, 0}, {276, 0}, {276, 120}, {0, 120}, {0, 12}};
    c->frame = lv_line_create(c->scr);
    lv_line_set_points(c->frame, pts, 6);
    lv_obj_set_style_line_width(c->frame, 2, 0);
    lv_obj_set_style_line_color(c->frame, COL_GREY, 0);
    lv_obj_set_pos(c->frame, 15, 59);
    lv_obj_clear_flag(c->frame, LV_OBJ_FLAG_CLICKABLE);
}

static void make_small_lamps(console_t *c)
{
    c->tx = box(c->scr, 252, 22, 14, 14, COL_PANEL);
    c->clip = box(c->scr, 316, 22, 14, 14, COL_PANEL);
    lv_obj_set_style_border_width(c->tx, 1, 0);
    lv_obj_set_style_border_color(c->tx, COL_FRAME, 0);
    lv_obj_set_style_border_width(c->clip, 1, 0);
    lv_obj_set_style_border_color(c->clip, COL_FRAME, 0);
}

/* battery bar: x 346-354, same span as the meter (y 49-178), no label;
 * drains from the top; green, yellow at 30 %, red at 15 % */
#define BATT_X 346
#define BATT_TOP 49
#define BATT_BOT 178
static void make_meter(console_t *c)
{
    for (int i = 0; i < METER_SEGS; i++)
        c->meter[i] = box(c->scr, 306, 178 - i * 11 - 8, 36, 8, COL_PANEL);
    c->batt = box(c->scr, BATT_X, BATT_TOP, 8, BATT_BOT - BATT_TOP, COL_GREEN);
}

static void batt_update(console_t *c)
{
    int pct = ui_platform_battery();
    if (!S.show_batt || pct < 0) { lv_obj_add_flag(c->batt, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_clear_flag(c->batt, LV_OBJ_FLAG_HIDDEN);
    if (pct > 100) pct = 100;
    int h = (BATT_BOT - BATT_TOP) * pct / 100;
    if (h < 2) h = 2;
    lv_obj_set_y(c->batt, BATT_BOT - h);
    lv_obj_set_height(c->batt, h);
    lv_obj_set_style_bg_color(c->batt, pct <= 15 ? COL_RED : pct <= 30 ? COL_AMBER : COL_GREEN, 0);
}

/* tilt lines glide toward the sensor angle; fade out while talking if QUIET ONLY */
static void tilt_update(console_t *c, bool keyed)
{
    float x = 0, y = 0;
    bool have = S.show_tilt && ui_platform_tilt(&x, &y);
    float target_opa = (!have || (S.tilt_quiet && keyed)) ? 0.0f : 255.0f;
    c->tilt_opa += (target_opa - c->tilt_opa) * 0.25f;
    if (have) {
        c->tilt_x += (x - c->tilt_x) * 0.2f;
        c->tilt_y += (y - c->tilt_y) * 0.2f;
    }
    float fx = c->tilt_x / TILT_RANGE, fy = c->tilt_y / TILT_RANGE;
    if (fx > 1) fx = 1; if (fx < -1) fx = -1;
    if (fy > 1) fy = 1; if (fy < -1) fy = -1;
    int cx = (SCOPE_X0 + SCOPE_X1) / 2 + (int)(fx * (SCOPE_X1 - SCOPE_X0) / 2);
    int cy = (SCOPE_Y0 + SCOPE_Y1) / 2 + (int)(fy * (SCOPE_Y1 - SCOPE_Y0) / 2);
    if (cx > SCOPE_X1 - 1) cx = SCOPE_X1 - 1;
    if (cy > SCOPE_Y1 - 1) cy = SCOPE_Y1 - 1;
    lv_obj_set_x(c->tilt_v, cx);
    lv_obj_set_y(c->tilt_h, cy);
    lv_opa_t o = (lv_opa_t)(c->tilt_opa < 2 ? 0 : c->tilt_opa);
    lv_obj_set_style_bg_opa(c->tilt_v, o, 0);
    lv_obj_set_style_bg_opa(c->tilt_h, o, 0);
}

static uint16_t lamp_delay(bool busy)
{
    return busy ? 10 + rnd() % 21          /* 1-3 s while transmitting */
                : 200 + rnd() % 201;       /* 20-40 s idle */
}

static void make_lamps(console_t *c)
{
    static const struct { int x, y, w, h; bool dome; } L[LAMPS] = {
        /* 4-row grid, 29 px pitch, row centres 7/36/65/94 (readout lines sit on the same rows) */
        {0, 0, 80, 14, false}, {0, 29, 32, 14, false}, {42, 22, 28, 28, true},
        {0, 58, 32, 14, false}, {42, 58, 32, 14, false}, {0, 80, 28, 28, true}, {42, 87, 32, 14, false},
    };
    for (int i = 0; i < LAMPS; i++) {
        lv_obj_t *o = box(c->scr, CLUSTER_X + L[i].x, CLUSTER_Y + L[i].y, L[i].w, L[i].h, COL_PANEL);
        if (L[i].dome) {
            lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
            lv_obj_t *hl = box(o, 6, 5, 6, 5, lv_color_white());   /* dome highlight */
            lv_obj_set_style_radius(hl, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(hl, LV_OPA_40, 0);
        }
        c->lamp_st[i] = rnd() % 4;
        c->lamp_next[i] = lamp_delay(false);
        lv_obj_set_style_bg_color(o, C(LAMP_HEX[c->lamp_st[i]]), 0);
        c->lamp[i] = o;
    }
}

static void lamp_flip(console_t *c, int i)
{
    c->lamp_st[i] = (c->lamp_st[i] + 1 + rnd() % 3) % 4;     /* always a different colour */
    lv_obj_set_style_bg_color(c->lamp[i], C(LAMP_HEX[c->lamp_st[i]]), 0);
}

/* readout line i: centred over the SYS key, vertically on lamp row i */
static void tick_place(console_t *c, int i)
{
    lv_obj_update_layout(c->tick[i]);
    lv_obj_set_pos(c->tick[i], TICK_CX - lv_obj_get_width(c->tick[i]) / 2,
                   CLUSTER_Y + LAMP_ROW[i] - lv_obj_get_height(c->tick[i]) / 2);
}

/* secret temp on readout line 0: "TEMP" and the unit in Aurebesh, the number in the
 * callsign's TK-style digits, centred as one group over the SYS key */
static void temp_update(console_t *c)
{
    float tc = 0;
    bool have = S.show_temp && ui_platform_temp(&tc);
    if (!have) {
        lv_obj_add_flag(c->t_word, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(c->t_num, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(c->t_unit, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(c->tick[0], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    c->temp_c += (tc - c->temp_c) * 0.3f;                  /* settle, don't flicker */
    lv_obj_add_flag(c->tick[0], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(c->t_word, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(c->t_num, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(c->t_unit, LV_OBJ_FLAG_HIDDEN);
    float v = S.temp_f ? c->temp_c * 9.0f / 5.0f + 32.0f : c->temp_c;
    int iv = (int)(v + (v < 0 ? -0.5f : 0.5f));
    lv_label_set_text_fmt(c->t_num, "%d", iv);
    lv_label_set_text_static(c->t_unit, S.temp_f ? "F" : "C");
    lv_obj_update_layout(c->t_word);
    int w1 = lv_obj_get_width(c->t_word), w2 = lv_obj_get_width(c->t_num), w3 = lv_obj_get_width(c->t_unit);
    int gap = 6, x = TICK_CX - (w1 + gap + w2 + 4 + w3) / 2, cy = CLUSTER_Y + LAMP_ROW[0];
    /* share one baseline: the two fonts pad their line boxes differently, so centring
     * the boxes would leave the digits sitting higher than the Aurebesh letters */
    int wy = cy - lv_obj_get_height(c->t_word) / 2;
    const lv_font_t *fw = lv_obj_get_style_text_font(c->t_word, 0);
    const lv_font_t *fn = lv_obj_get_style_text_font(c->t_num, 0);
    int baseline = wy + fw->line_height - fw->base_line;
    lv_obj_set_pos(c->t_word, x, wy);
    lv_obj_set_pos(c->t_num, x + w1 + gap, baseline - (fn->line_height - fn->base_line));
    lv_obj_set_pos(c->t_unit, x + w1 + gap + w2 + 4, wy);
}

static void make_ticker(console_t *c)
{
    for (int i = 0; i < TICK_LINES; i++) {
        c->tick_word[i] = (i * 5 + (c == &con_lock ? 3 : 0)) % NWORDS;
        c->tick[i] = aureb_label(c->scr, WORDS[c->tick_word[i]], f_aureb13, COL_TICK);
        tick_place(c, i);
    }
    c->tick_next = 8;
    c->t_word = aureb_label(c->scr, "TEMP", f_aureb13, COL_TICK);
    c->t_num = label(c->scr, "0", f_aureng20 ? f_aureng20 : &lv_font_montserrat_16, COL_TICK);   /* 20 px: digits 13 px tall = the Aurebesh letters as drawn */
    c->t_unit = aureb_label(c->scr, "F", f_aureb13, COL_TICK);
    c->temp_c = 30;
}

static void tick_change(console_t *c)
{
    int first = S.show_temp ? 1 : 0;                       /* line 0 holds the temp readout */
    int i = first + rnd() % (TICK_LINES - first), w;
    for (;;) {
        w = rnd() % NWORDS;
        bool used = false;
        for (int k = 0; k < TICK_LINES; k++) used |= (c->tick_word[k] == w);
        if (!used) break;
    }
    c->tick_word[i] = w;
    lv_label_set_text_static(c->tick[i], WORDS[w]);
    tick_place(c, i);
}

static lv_obj_t *ring(lv_obj_t *p, int cx, int cy, int r, int w, int span, lv_color_t col)
{
    lv_obj_t *a = lv_arc_create(p);
    lv_obj_set_size(a, r * 2, r * 2);
    lv_obj_set_pos(a, cx - r, cy - r);
    lv_arc_set_bg_angles(a, 0, 0);
    lv_obj_set_style_arc_opa(a, LV_OPA_0, LV_PART_MAIN);
    lv_arc_set_angles(a, 0, span);
    lv_obj_set_style_arc_width(a, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, col, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, false, LV_PART_INDICATOR);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    return a;
}

/* Scomp link: outer 3-segment ring, inner rotor, fast blue tip (socket + hub are in the skin) */
static void make_scomp(console_t *c)
{
    for (int i = 0; i < 3; i++) c->ring_outer[i] = ring(c->scr, SCOMP_CX, SCOMP_CY, 42, 8, 80, C(0x8A9095));
    c->ring_inner = ring(c->scr, SCOMP_CX, SCOMP_CY, 28, 7, 250, COL_FRAME);
    c->ring_tip[0] = ring(c->scr, SCOMP_CX, SCOMP_CY, 16, 4, 60, COL_BLUE);
    c->ring_tip[1] = ring(c->scr, SCOMP_CX, SCOMP_CY, 16, 4, 60, COL_BLUE);
    c->ang[0] = 0; c->ang[1] = 90; c->ang[2] = 0;
    c->spd[0] = 2; c->spd[1] = -4; c->spd[2] = 9;       /* deg per 50 ms tick */
    for (int r = 0; r < 3; r++) c->rev_next[r] = 160 + rnd() % 161;   /* 8-16 s */
}

static void console_build(console_t *c, lv_color_t trace)
{
    make_small_lamps(c);
    make_chart(c, trace);
    make_meter(c);
    make_lamps(c);
    make_ticker(c);
    make_scomp(c);
}

/* ================= MAIN ================= */
static lv_obj_t *scr_settings, *scr_callsign;
static cap_t cap_sys, cap_live;
static lv_obj_t *lock_cover, *lock_cover_lbl, *lock_lever, *lock_knob;
static int lock_cover_state;            /* 0 closed, 1 open */
static lv_timer_t *lock_cover_timer;
static bool shown_muted = true;

static void open_settings(int page);
static void go_lock(void);

static void live_refresh(void)
{
    bool m = audio_is_muted();
    shown_muted = m;
    lv_label_set_text(cap_live.lbl, m ? "MUTED" : "LIVE");
    /* two lines so it stays at the full 14 px instead of shrinking */
    lv_label_set_text(cap_live.sub, m ? "TAP FOR\nLIVE" : "TRANSMIT");
    lv_obj_set_style_text_align(cap_live.sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(cap_live.sub, -2, 0);
    lv_obj_set_style_text_font(cap_live.sub, &lv_font_montserrat_14, 0);
    cap_style(&cap_live, !m);
    lv_obj_align(cap_live.lbl, LV_ALIGN_CENTER, 0, m ? -17 : -10);
    lv_obj_align(cap_live.sub, LV_ALIGN_CENTER, 0, m ? 13 : 13);
}

static void live_cb(lv_event_t *e) { audio_set_mute(!audio_is_muted()); live_refresh(); }
static void sys_cb(lv_event_t *e) { open_settings(0); }

/* lever + knob are children of the LOCK hit area at (16, 320); switch nut centre = (52, 63) local */
static void lever_set(bool down)
{
    if (down) { lv_obj_set_pos(lock_lever, 47, 63); lv_obj_set_pos(lock_knob, 45, 97); }
    else      { lv_obj_set_pos(lock_lever, 47, 23); lv_obj_set_pos(lock_knob, 45, 17); }
}

static void cover_set(int open)
{
    lock_cover_state = open;
    lv_obj_set_height(lock_cover, open ? 14 : 84);
    if (open) lv_obj_add_flag(lock_cover_lbl, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(lock_cover_lbl, LV_OBJ_FLAG_HIDDEN);
}

static void cover_close_cb(lv_timer_t *t) { cover_set(0); lock_cover_timer = NULL; }

static void lock_throw_cb(lv_timer_t *t)
{
    lever_set(false);
    cover_set(0);
    go_lock();
}

/* tap 1 flips the safety cover up (closes again after 3 s); tap 2 throws the switch */
static void lock_tap_cb(lv_event_t *e)
{
    if (lock_cover_state == 0) {
        cover_set(1);
        lock_cover_timer = lv_timer_create(cover_close_cb, 3000, NULL);
        lv_timer_set_repeat_count(lock_cover_timer, 1);
    } else {
        if (lock_cover_timer) { lv_timer_del(lock_cover_timer); lock_cover_timer = NULL; }
        lever_set(true);
        lv_timer_t *t = lv_timer_create(lock_throw_cb, 350, NULL);
        lv_timer_set_repeat_count(t, 1);
    }
}

static void build_main(void)
{
    console_t *c = &con_main;
    c->scr = make_screen(&skin_main, NULL);
    c->callsign = label(c->scr, S.callsign, &lv_font_montserrat_22, COL_RED);
    callsign_show(c);
    console_build(c, COL_BLUE);

    /* LOCK: guarded toggle (nut is in the skin) */
    lv_obj_t *hit = lv_obj_create(c->scr);                    /* touch area = whole housing */
    lv_obj_remove_style_all(hit);
    lv_obj_set_pos(hit, 16, 320);
    lv_obj_set_size(hit, 104, 114);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hit, lock_tap_cb, LV_EVENT_CLICKED, NULL);
    lock_lever = box(hit, 0, 0, 10, 40, C(0xB9BEC2));
    lv_obj_set_style_radius(lock_lever, 4, 0);
    lv_obj_set_style_border_width(lock_lever, 1, 0);
    lv_obj_set_style_border_color(lock_lever, C(0x6E757B), 0);
    lock_knob = box(hit, 0, 0, 14, 14, C(0xD8DCDF));
    lv_obj_set_style_radius(lock_knob, LV_RADIUS_CIRCLE, 0);
    lever_set(false);

    lock_cover = box(hit, 14, 8, 76, 84, C(0x6E1015));
    lv_obj_set_style_bg_opa(lock_cover, 215, 0);
    lv_obj_set_style_radius(lock_cover, 5, 0);
    lv_obj_set_style_border_width(lock_cover, 2, 0);
    lv_obj_set_style_border_color(lock_cover, COL_RED, 0);
    lv_obj_set_style_clip_corner(lock_cover, true, 0);
    static lv_point_t stripe[2] = {{0, 84}, {84, 0}};
    for (int k = -84; k < 76; k += 16) {                       /* hazard stripes */
        lv_obj_t *ln = lv_line_create(lock_cover);
        lv_line_set_points(ln, stripe, 2);
        lv_obj_set_style_line_width(ln, 6, 0);
        lv_obj_set_style_line_color(ln, C(0x1A0405), 0);
        lv_obj_set_pos(ln, k, 0);
    }
    lock_cover_lbl = label(lock_cover, "LOCK", &lv_font_montserrat_16, C(0xF0C8C8));
    lv_obj_center(lock_cover_lbl);
    lv_obj_t *hinge = box(hit, 34, 5, 36, 6, C(0x8A9095));
    (void)hinge;

    cap_sys = make_cap(c->scr, 132, 320, 104, 114, "SYS", "SETTINGS", COL_BLUE, false, sys_cb, LV_EVENT_CLICKED, NULL);
    cap_live = make_cap(c->scr, 248, 320, 104, 114, "MUTED", "TAP FOR LIVE", COL_RED, false, live_cb, LV_EVENT_CLICKED, NULL);
    live_refresh();
}

/* ================= LOCK ================= */
static lv_obj_t *lock_bar, *lock_hex[2], *lock_sig[5];
static uint32_t tap_times[5];
static int tap_idx;

static void lock_screen_tap_cb(lv_event_t *e)
{
    uint32_t now = lv_tick_get();
    tap_times[tap_idx % 5] = now;
    tap_idx++;
    lv_obj_set_style_text_color(lock_hex[0], COL_RED, 0);       /* blip */
    if (tap_idx >= 5 && now - tap_times[tap_idx % 5] <= 2500) {
        tap_idx = 0;
        callsign_show(&con_main);
        lv_scr_load(con_main.scr);
    }
}

static void go_lock(void)
{
    tap_idx = 0;
    callsign_show(&con_lock);
    lv_scr_load(con_lock.scr);
}

static void build_lock(void)
{
    console_t *c = &con_lock;
    c->scr = make_screen(&skin_lock, NULL);
    lv_obj_add_flag(c->scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c->scr, lock_screen_tap_cb, LV_EVENT_CLICKED, NULL);
    c->callsign = label(c->scr, S.callsign, &lv_font_montserrat_22, COL_RED);
    callsign_show(c);
    console_build(c, COL_RED);
    lock_bar = box(c->scr, 52, 386, 32, 10, COL_RED);
    for (int i = 0; i < 2; i++) {
        lock_hex[i] = label(c->scr, "0000", &lv_font_montserrat_20, COL_AMBER);
        center_at(lock_hex[i], 184, 378 + i * 24);
    }
    for (int i = 0; i < 5; i++) lock_sig[i] = box(c->scr, 264 + i * 15, 394, 10, 20, COL_GREEN);
    /* every child passes taps through to the screen */
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(c->scr); i++)
        lv_obj_clear_flag(lv_obj_get_child(c->scr, i), LV_OBJ_FLAG_CLICKABLE);
}

/* ================= SETTINGS ================= */
enum { P_VOLUME, P_EFFECT, P_VOX, P_STATIC, P_CLICK, P_BRIGHT, P_CALLSIGN, P_RESET, P_DISPLAY, P_SENSORS, NPAGES };
static const struct { const char *title, *desc; int min, max; } PAGE[NPAGES] = {
    {"VOLUME", "SPEAKER LEVEL", 0, 100},
    {"EFFECT", "DRIVE + BITCRUSH", 0, 100},
    {"VOX", "GATE OPENS AT", 0, 100},
    {"STATIC", "RADIO NOISE BED", 0, 100},
    {"CLICK", "KEY-UP CLICK", 0, 100},
    {"BRIGHTNESS", "PANEL LIGHT", 10, 100},
    {"CALLSIGN", "", 0, 0},
    {"FACTORY RESET", "", 0, 0},
    {"DISPLAY OFF", "", 0, 0},
    {"SENSORS", "", 0, 0},
};
static int page;
static lv_obj_t *set_bg, *set_title, *set_count, *set_pip;
static lv_obj_t *grp_value, *grp_cs, *grp_reset, *grp_disp, *grp_sens;   /* per-page content groups */
#define NTOG 5
static cap_t cap_tog[NTOG];          /* 0 BATTERY 1 TILT GRID 2 QUIET ONLY 3 TEMP 4 C/F */
static lv_obj_t *sens_batt, *sens_tilt, *sens_temp;
static lv_obj_t *g_arc, *g_val, *g_desc, *g_bar[10], *g_tick[2], *g_lamp[3], *g_spin;
static lv_obj_t *cs_show;
static lv_obj_t *rs_seg[20];
static cap_t cap_minus, cap_plus, cap_wide;
static uint32_t reset_t0;
static bool reset_held;
static int spin_ang;

static int *page_val(int p)
{
    switch (p) {
    case P_VOLUME: return &S.volume;
    case P_EFFECT: return &S.effect;
    case P_VOX: return &S.vox;
    case P_STATIC: return &S.stat;
    case P_CLICK: return &S.click;
    case P_BRIGHT: return &S.brightness;
    default: return NULL;
    }
}

static lv_obj_t *group(lv_obj_t *p)
{
    lv_obj_t *g = lv_obj_create(p);
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, SCR_W, SCR_H);
    lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return g;
}

static void value_refresh(void)
{
    int *v = page_val(page);
    if (!v) return;
    lv_arc_set_range(g_arc, PAGE[page].min, PAGE[page].max);
    lv_arc_set_value(g_arc, *v);
    lv_label_set_text_fmt(g_val, "%d", *v);
    center_at(g_val, 98, 118);
    int tenths = (*v - PAGE[page].min) * 10 / (PAGE[page].max - PAGE[page].min);
    for (int i = 0; i < 10; i++)
        lv_obj_set_style_bg_opa(g_bar[i], i < tenths ? LV_OPA_COVER : LV_OPA_0, 0);
}

static void toggles_refresh(void);
static void sensors_readout(void);

static void page_show(void)
{
    lv_label_set_text(set_title, PAGE[page].title);
    lv_label_set_text_fmt(set_count, "%d/%d", page + 1, NPAGES);
    lv_obj_update_layout(set_count);
    lv_obj_set_pos(set_count, 322 - lv_obj_get_width(set_count), 19);
    lv_obj_set_x(set_pip, 46 + page * 27);
    bool three = (page == P_SENSORS);
    bool wide = (page == P_CALLSIGN || page == P_RESET || page == P_DISPLAY);
    lv_img_set_src(set_bg, three ? &skin_set_sensors : wide ? &skin_set_wide : &skin_set_value);
    if (wide || three) { lv_obj_add_flag(grp_value, LV_OBJ_FLAG_HIDDEN); }
    else { lv_obj_clear_flag(grp_value, LV_OBJ_FLAG_HIDDEN); }
    if (three) lv_obj_clear_flag(grp_sens, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(grp_sens, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < NTOG; i++) {
        if (three) lv_obj_clear_flag(cap_tog[i].cap, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(cap_tog[i].cap, LV_OBJ_FLAG_HIDDEN);
    }
    if (page == P_CALLSIGN) lv_obj_clear_flag(grp_cs, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(grp_cs, LV_OBJ_FLAG_HIDDEN);
    if (page == P_RESET) lv_obj_clear_flag(grp_reset, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(grp_reset, LV_OBJ_FLAG_HIDDEN);
    if (page == P_DISPLAY) lv_obj_clear_flag(grp_disp, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(grp_disp, LV_OBJ_FLAG_HIDDEN);
    if (wide) lv_obj_clear_flag(cap_wide.cap, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(cap_wide.cap, LV_OBJ_FLAG_HIDDEN);
    if (!wide && !three) { lv_obj_clear_flag(cap_minus.cap, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(cap_plus.cap, LV_OBJ_FLAG_HIDDEN); }
    else { lv_obj_add_flag(cap_minus.cap, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(cap_plus.cap, LV_OBJ_FLAG_HIDDEN); }

    if (page == P_CALLSIGN) {
        cap_wide.accent = COL_AMBER;
        lv_label_set_text(cap_wide.lbl, "EDIT CALLSIGN");
        lv_label_set_text(cap_wide.sub, "OPENS CODE CYLINDER");
        cap_style(&cap_wide, false);
        lv_label_set_text(cs_show, S.callsign);
        center_at(cs_show, 184, 112);
    } else if (page == P_RESET) {
        cap_wide.accent = COL_RED;
        lv_label_set_text(cap_wide.lbl, "HOLD TO ERASE");
        lv_label_set_text(cap_wide.sub, "ARMING 0%");
        cap_style(&cap_wide, true);
        for (int i = 0; i < 20; i++) lv_obj_set_style_bg_color(rs_seg[i], COL_DIM, 0);
    } else if (page == P_DISPLAY) {
        cap_wide.accent = COL_AMBER;
        lv_label_set_text(cap_wide.lbl, "TURN DISPLAY OFF");
        lv_label_set_text(cap_wide.sub, "TAP 5 TIMES TO WAKE");
        cap_style(&cap_wide, true);
    } else if (page == P_SENSORS) {
        toggles_refresh();
        sensors_readout();
    } else {
        lv_label_set_text(g_desc, PAGE[page].desc);
        value_refresh();
    }
    lv_obj_align(cap_wide.lbl, LV_ALIGN_CENTER, 0, -10);
    lv_obj_align(cap_wide.sub, LV_ALIGN_CENTER, 0, 13);
}

/* ---- sensors page: three on/off keys (lit green = on) + live readings ---- */
static uint8_t *tog_val(int i)
{
    switch (i) {
    case 0: return &S.show_batt;
    case 1: return &S.show_tilt;
    case 2: return &S.tilt_quiet;
    case 3: return &S.show_temp;
    default: return &S.temp_f;
    }
}

static void toggles_refresh(void)
{
    for (int i = 0; i < 4; i++) {
        bool on = *tog_val(i);
        lv_label_set_text(cap_tog[i].sub, on ? "ON" : "OFF");
        cap_style(&cap_tog[i], on);
    }
    /* unit selector: not on/off, so never lit */
    lv_label_set_text(cap_tog[4].lbl, S.temp_f ? "F" : "C");
    lv_label_set_text(cap_tog[4].sub, "UNIT");          /* the big F / C says which */
    sub_fit(&cap_tog[4]);
    lv_obj_align(cap_tog[4].lbl, LV_ALIGN_CENTER, 0, -12);
    lv_obj_align(cap_tog[4].sub, LV_ALIGN_CENTER, 0, 16);
}

static void toggle_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    *tog_val(i) = !*tog_val(i);
    toggles_refresh();
    ui_platform_apply(&S);
}

static void sensors_readout(void)
{
    if (page != P_SENSORS) return;
    int pct = ui_platform_battery();
    if (pct < 0) lv_label_set_text(sens_batt, "PWR  USB");
    else lv_label_set_text_fmt(sens_batt, "PWR  %d%%", pct);
    float x = 0, y = 0;
    if (ui_platform_tilt(&x, &y))
        lv_label_set_text_fmt(sens_tilt, "TILT %+d %+d", (int)x, (int)y);
    else
        lv_label_set_text(sens_tilt, "TILT  --");
    float tc;
    if (ui_platform_temp(&tc)) {
        float v = S.temp_f ? tc * 9.0f / 5.0f + 32.0f : tc;
        lv_label_set_text_fmt(sens_temp, "TEMP %d %s", (int)(v + 0.5f), S.temp_f ? "F" : "C");
    } else {
        lv_label_set_text(sens_temp, "TEMP  --");
    }
}

static void open_settings(int p)
{
    page = p;
    page_show();
    lv_scr_load(scr_settings);
}

static void step_cb(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int *v = page_val(page);
    if (!v) return;
    *v += dir * (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED_REPEAT ? 2 : 1);
    if (*v < PAGE[page].min) *v = PAGE[page].min;
    if (*v > PAGE[page].max) *v = PAGE[page].max;
    value_refresh();
    ui_platform_apply(&S);
}

static void nav_cb(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    if (dir == 0) {
        callsign_show(&con_main);
        lv_scr_load(con_main.scr);
        return;
    }
    page = (page + dir + NPAGES) % NPAGES;
    page_show();
}

static void open_callsign(void);
static void display_off(void);

static void wide_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (page == P_CALLSIGN) {
        if (code == LV_EVENT_CLICKED) open_callsign();
        return;
    }
    if (page == P_DISPLAY) {
        if (code == LV_EVENT_CLICKED) display_off();
        return;
    }
    if (page == P_RESET) {
        if (code == LV_EVENT_PRESSED) { reset_held = true; reset_t0 = lv_tick_get(); }
        if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            reset_held = false;
            lv_label_set_text(cap_wide.sub, "ARMING 0%");
            for (int i = 0; i < 20; i++) lv_obj_set_style_bg_color(rs_seg[i], COL_DIM, 0);
        }
    }
}

static void reset_tick(void)
{
    if (!reset_held || page != P_RESET || lv_scr_act() != scr_settings) return;
    uint32_t el = lv_tick_elaps(reset_t0);
    int pct = el >= 2000 ? 100 : (int)(el * 100 / 2000);
    lv_label_set_text_fmt(cap_wide.sub, "ARMING %d%%", pct);
    for (int i = 0; i < 20; i++) lv_obj_set_style_bg_color(rs_seg[i], i < pct / 5 ? COL_RED : COL_DIM, 0);
    if (pct >= 100) {
        reset_held = false;
        ui_platform_defaults(&S);
        ui_platform_apply(&S);
        callsign_show(&con_main);
        lv_scr_load(con_main.scr);
    }
}

static void build_settings(void)
{
    scr_settings = make_screen(&skin_set_value, &set_bg);
    set_title = label(scr_settings, "", &lv_font_montserrat_22, COL_TEXT);
    lv_obj_set_pos(set_title, 46, 15);
    set_count = label(scr_settings, "", &lv_font_montserrat_16, COL_LABEL);
    set_pip = box(scr_settings, 46, 46, 22, 4, COL_AMBER);

    /* value pages */
    grp_value = group(scr_settings);
    g_spin = ring(grp_value, 98, 122, 59, 3, 60, COL_FRAME);
    g_arc = lv_arc_create(grp_value);
    lv_obj_set_size(g_arc, 100, 100);
    lv_obj_set_pos(g_arc, 48, 72);
    lv_arc_set_bg_angles(g_arc, 135, 45);
    lv_obj_set_style_arc_opa(g_arc, LV_OPA_0, LV_PART_MAIN);       /* track is in the skin */
    lv_obj_set_style_arc_width(g_arc, 9, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_arc, COL_AMBER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_arc, false, LV_PART_INDICATOR);
    lv_obj_remove_style(g_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(g_arc, LV_OBJ_FLAG_CLICKABLE);
    g_val = label(grp_value, "0", &lv_font_montserrat_28, COL_AMBER);
    g_desc = label(grp_value, "", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(g_desc, 176, 68);
    for (int i = 0; i < 10; i++) g_bar[i] = box(grp_value, 176 + i * 17, 94, 13, 18, COL_AMBER);
    for (int i = 0; i < 2; i++) {
        g_tick[i] = aureb_label(grp_value, WORDS[(i * 7 + 2) % NWORDS], f_aureb12, C(0x7B5B15));
        lv_obj_set_pos(g_tick[i], 176, 122 + i * 22);
    }
    for (int i = 0; i < 3; i++) g_lamp[i] = box(grp_value, 176 + i * 22, 168, 16, 8, C(LAMP_HEX[(i + 3) % 4]));

    /* callsign page */
    grp_cs = group(scr_settings);
    cs_show = label(grp_cs, "", f_aureng28 ? f_aureng28 : &lv_font_montserrat_28, COL_RED);
    lv_obj_t *cs_note = label(grp_cs, "CURRENT CALLSIGN", &lv_font_montserrat_14, COL_LABEL);
    center_at(cs_note, 184, 78);

    /* reset page */
    grp_reset = group(scr_settings);
    lv_obj_t *r1 = label(grp_reset, "Erase callsign and settings", &lv_font_montserrat_14, COL_TEXT);
    center_at(r1, 184, 88);
    lv_obj_t *r2 = label(grp_reset, "Hold the switch for 2 seconds", &lv_font_montserrat_14, COL_LABEL);
    center_at(r2, 184, 110);
    for (int i = 0; i < 20; i++) rs_seg[i] = box(grp_reset, 46 + i * 14, 138, 10, 22, COL_DIM);

    /* sensors page */
    /* layout: top row TEMP | C/F | readings panel; row A BATTERY | TILT GRID | QUIET ONLY */
    grp_sens = group(scr_settings);
    sens_batt = label(grp_sens, "", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(sens_batt, 258, 106);
    sens_tilt = label(grp_sens, "", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(sens_tilt, 258, 132);
    sens_temp = label(grp_sens, "", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(sens_temp, 258, 158);

    /* display-off page */
    grp_disp = group(scr_settings);
    lv_obj_t *d1 = label(grp_disp, "Blank the screen. Voice keeps working,", &lv_font_montserrat_14, COL_TEXT);
    center_at(d1, 184, 88);
    lv_obj_t *d2 = label(grp_disp, "BOOT still switches LIVE / MUTED.", &lv_font_montserrat_14, COL_TEXT);
    center_at(d2, 184, 110);
    lv_obj_t *d3 = label(grp_disp, "Tap the screen 5 times to wake it.", &lv_font_montserrat_14, COL_AMBER);
    center_at(d3, 184, 146);

    cap_minus = make_cap(scr_settings, 16, 204, 160, 104, LV_SYMBOL_MINUS, "HOLD TO REPEAT", COL_BLUE, true,
                         step_cb, LV_EVENT_PRESSED, (void *)(intptr_t)-1);
    cap_plus = make_cap(scr_settings, 192, 204, 160, 104, LV_SYMBOL_PLUS, "HOLD TO REPEAT", COL_BLUE, true,
                        step_cb, LV_EVENT_PRESSED, (void *)(intptr_t)1);
    cap_wide = make_cap(scr_settings, 16, 204, 336, 104, "EDIT", "", COL_AMBER, false, wide_cb, LV_EVENT_ALL, NULL);
    static const char *TOG[NTOG] = {"BATTERY", "TILT\nGRID", "QUIET\nONLY", "TEMP", "F"};
    static const int TX[NTOG] = {16, 132, 248, 16, 132}, TY[NTOG] = {204, 204, 204, 88, 88};
    for (int i = 0; i < NTOG; i++) {
        cap_tog[i] = make_cap(scr_settings, TX[i], TY[i], 104, 104, TOG[i], "ON", i == 4 ? COL_AMBER : COL_GREEN,
                              false, toggle_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        if (i == 4) lv_obj_set_style_text_font(cap_tog[i].lbl, &lv_font_montserrat_28, 0);
        /* names 16 px so the five keys match; 14 px if 16 would leave < 8 px each side of the cap */
        if (i != 4) {
            lv_point_t sz;
            lv_obj_set_style_text_font(cap_tog[i].lbl, &lv_font_montserrat_16, 0);
            lv_txt_get_size(&sz, TOG[i], &lv_font_montserrat_16, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (sz.x > 66) lv_obj_set_style_text_font(cap_tog[i].lbl, &lv_font_montserrat_14, 0);
        }
        if (strchr(TOG[i], '\n')) {
            lv_obj_set_style_text_align(cap_tog[i].lbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_line_space(cap_tog[i].lbl, -2, 0);
            lv_obj_align(cap_tog[i].lbl, LV_ALIGN_CENTER, 0, -11);
            lv_obj_align(cap_tog[i].sub, LV_ALIGN_CENTER, 0, 22);
        }
    }
    make_cap(scr_settings, 16, 320, 104, 114, LV_SYMBOL_LEFT, "PREV", COL_BLUE, true, nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    make_cap(scr_settings, 132, 320, 104, 114, "MAIN", "EXIT", COL_TEXT, false, nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)0);
    make_cap(scr_settings, 248, 320, 104, 114, LV_SYMBOL_RIGHT, "NEXT", COL_BLUE, true, nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
}

/* ================= CALLSIGN (code cylinder) ================= */
static const char CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
#define NCHARS ((int)sizeof(CHARS) - 1)
static char cs_buf[UI_CALLSIGN_MAX + 1];
static int cs_sel;
static lv_obj_t *drum_lbl[UI_CALLSIGN_MAX][3], *drum_sel, *drum_mark, *cs_preview;

static int char_idx(char ch) { const char *p = strchr(CHARS, ch); return p && ch ? (int)(p - CHARS) : 0; }
static char shown(char ch) { return ch == ' ' ? '.' : ch; }
static int drum_x(int i) { return 25 + i * 32; }

static void drums_refresh(void)
{
    static char txt[UI_CALLSIGN_MAX][3][2];
    for (int i = 0; i < UI_CALLSIGN_MAX; i++) {
        int k = char_idx(cs_buf[i]);
        char t[3] = {CHARS[(k + NCHARS - 1) % NCHARS], cs_buf[i], CHARS[(k + 1) % NCHARS]};
        for (int r = 0; r < 3; r++) {
            txt[i][r][0] = shown(t[r]);
            txt[i][r][1] = 0;
            lv_label_set_text_static(drum_lbl[i][r], txt[i][r]);
            center_at(drum_lbl[i][r], drum_x(i) + 14, 70 + r * 32);
        }
        lv_obj_set_style_text_color(drum_lbl[i][1], i == cs_sel ? COL_AMBER : COL_TEXT, 0);
    }
    lv_obj_set_x(drum_sel, drum_x(cs_sel));
    lv_obj_set_x(drum_mark, drum_x(cs_sel) + 8);
    char prev[UI_CALLSIGN_MAX + 1];
    memcpy(prev, cs_buf, sizeof(prev));
    for (int i = UI_CALLSIGN_MAX - 1; i >= 0 && prev[i] == ' '; i--) prev[i] = 0;
    lv_label_set_text(cs_preview, prev);
    center_at(cs_preview, 184, 174);
}

static void open_callsign(void)
{
    memset(cs_buf, ' ', UI_CALLSIGN_MAX);
    cs_buf[UI_CALLSIGN_MAX] = 0;
    memcpy(cs_buf, S.callsign, strlen(S.callsign));
    cs_sel = 0;
    drums_refresh();
    lv_scr_load(scr_callsign);
}

static void cs_char_cb(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int k = char_idx(cs_buf[cs_sel]);
    cs_buf[cs_sel] = CHARS[(k + dir + NCHARS) % NCHARS];
    drums_refresh();
}

static void cs_pos_cb(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    cs_sel = (cs_sel + dir + UI_CALLSIGN_MAX) % UI_CALLSIGN_MAX;
    drums_refresh();
}

static void cs_save_cb(lv_event_t *e)
{
    char out[UI_CALLSIGN_MAX + 1];
    memcpy(out, cs_buf, sizeof(out));
    for (int i = UI_CALLSIGN_MAX - 1; i >= 0 && out[i] == ' '; i--) out[i] = 0;
    if (out[0]) {
        memcpy(S.callsign, out, sizeof(out));
        ui_platform_apply(&S);
    }
    open_settings(P_CALLSIGN);
}

static void build_callsign(void)
{
    scr_callsign = make_screen(&skin_callsign, NULL);
    for (int i = 0; i < UI_CALLSIGN_MAX; i++)
        for (int r = 0; r < 3; r++)
            drum_lbl[i][r] = label(scr_callsign, "", r == 1 ? &lv_font_montserrat_18 : &lv_font_montserrat_14,
                                   r == 1 ? COL_TEXT : C(0x3E4448));
    drum_sel = box(scr_callsign, 0, 58, 29, 89, COL_AMBER);
    lv_obj_set_style_bg_opa(drum_sel, LV_OPA_0, 0);
    lv_obj_set_style_border_width(drum_sel, 2, 0);
    lv_obj_set_style_border_color(drum_sel, COL_AMBER, 0);
    drum_mark = box(scr_callsign, 0, 151, 13, 5, COL_AMBER);
    cs_preview = label(scr_callsign, "", f_aureng20 ? f_aureng20 : &lv_font_montserrat_20, COL_RED);
    make_cap(scr_callsign, 16, 204, 160, 104, LV_SYMBOL_UP, "NEXT CHAR", COL_AMBER, true,
             cs_char_cb, LV_EVENT_PRESSED, (void *)(intptr_t)1);
    make_cap(scr_callsign, 192, 204, 160, 104, LV_SYMBOL_DOWN, "PREV CHAR", COL_AMBER, true,
             cs_char_cb, LV_EVENT_PRESSED, (void *)(intptr_t)-1);
    make_cap(scr_callsign, 16, 320, 104, 114, LV_SYMBOL_LEFT, "POSITION", COL_BLUE, true,
             cs_pos_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    cap_t save = make_cap(scr_callsign, 132, 320, 104, 114, "SAVE", "COMMIT", COL_GREEN, false,
                          cs_save_cb, LV_EVENT_CLICKED, NULL);
    cap_style(&save, true);
    make_cap(scr_callsign, 248, 320, 104, 114, LV_SYMBOL_RIGHT, "POSITION", COL_BLUE, true,
             cs_pos_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
}

/* ================= DISPLAY OFF (blank screen) ================= */
/* Everything black (AMOLED pixels off) except one dim line of text in the middle, which
 * drifts a few px every 20 s so it can't burn in. 5 quick taps wake the console. */
static lv_obj_t *scr_blank, *blank_lbl;
static uint32_t wake_times[5];
static int wake_idx;

static void blank_drift_cb(lv_timer_t *t)
{
    if (lv_scr_act() != scr_blank) return;
    center_at(blank_lbl, SCR_W / 2 + (int)(rnd() % 41) - 20, SCR_H / 2 + (int)(rnd() % 61) - 30);
}

static void blank_tap_cb(lv_event_t *e)
{
    uint32_t now = lv_tick_get();
    wake_times[wake_idx % 5] = now;
    wake_idx++;
    lv_obj_set_style_text_color(blank_lbl, COL_AMBER, 0);             /* acknowledge the tap */
    if (wake_idx >= 5 && now - wake_times[wake_idx % 5] <= 2500) {
        wake_idx = 0;
        callsign_show(&con_main);
        lv_scr_load(con_main.scr);
    }
}

static void display_off(void)
{
    wake_idx = 0;
    lv_obj_set_style_text_color(blank_lbl, C(0x6B5212), 0);           /* dim amber */
    center_at(blank_lbl, SCR_W / 2, SCR_H / 2);
    lv_scr_load(scr_blank);
}

static void build_blank(void)
{
    scr_blank = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_blank, lv_color_black(), 0);
    lv_obj_clear_flag(scr_blank, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scr_blank, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr_blank, blank_tap_cb, LV_EVENT_CLICKED, NULL);
    blank_lbl = label(scr_blank, "TAP 5 TIMES\nTO WAKE DISPLAY", &lv_font_montserrat_18, C(0x6B5212));
    lv_obj_set_style_text_align(blank_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_clear_flag(blank_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_timer_create(blank_drift_cb, 20000, NULL);
}

/* ================= life: timers ================= */
static console_t *active_console(void)
{
    lv_obj_t *a = lv_scr_act();
    if (a == con_main.scr) return &con_main;
    if (a == con_lock.scr) return &con_lock;
    return NULL;
}

static bool was_keyed;
static float boost = 1.0f;

/* 40 ms: scope, meter, TX/CLIP, frame colour, LIVE state */
static void fast_cb(lv_timer_t *t)
{
    if (shown_muted != audio_is_muted()) live_refresh();
    reset_tick();
    console_t *c = active_console();
    if (!c) return;
    int lvl = audio_get_level();
    bool keyed = audio_is_keyed(), muted = audio_is_muted();
    lv_chart_set_next_value(c->chart, c->ser, lvl < 2 ? 2 : lvl);
    int lit = lvl * METER_SEGS / 100;
    for (int i = 0; i < METER_SEGS; i++)
        lv_obj_set_style_bg_color(c->meter[i], i < lit ? (i >= 9 ? COL_RED : COL_GREEN) : COL_PANEL, 0);
    lv_obj_set_style_bg_color(c->tx, keyed && !muted ? COL_RED : COL_PANEL, 0);
    lv_obj_set_style_bg_color(c->clip, audio_get_clip() ? COL_RED : COL_PANEL, 0);
    lv_color_t fc = keyed ? (muted ? COL_AMBER : COL_RED) : COL_GREY;
    lv_obj_set_style_line_color(c->frame, fc, 0);
    lv_obj_set_style_line_width(c->frame, keyed ? 3 : 2, 0);
    batt_update(c);
    tilt_update(c, keyed);
}

/* 50 ms: Scomp rings (random speed/direction, spin up while keyed) + settings spinner */
static void ring_cb(lv_timer_t *t)
{
    bool keyed = audio_is_keyed(), muted = audio_is_muted();
    float target = keyed ? (muted ? 1.4f : 2.6f) : 1.0f;
    boost += (target - boost) * (target > boost ? 0.30f : 0.08f);   /* spin up fast, wind down slow */
    console_t *c = active_console();
    if (c) {
        static const float base[3] = {2, 4, 9};
        for (int r = 0; r < 3; r++) {
            if (--c->rev_next[r] == 0) {                               /* reverse every 8-16 s, */
                float m = (float)(1 + rnd() % 3) * base[r] / 2.0f;     /* at a new random speed */
                c->spd[r] = c->spd[r] > 0 ? -m : m;
                c->rev_next[r] = 160 + rnd() % 161;
            }
            c->ang[r] += c->spd[r] * boost;
            while (c->ang[r] >= 360) c->ang[r] -= 360;
            while (c->ang[r] < 0) c->ang[r] += 360;
        }
        for (int i = 0; i < 3; i++) lv_arc_set_rotation(c->ring_outer[i], (int)c->ang[0] + i * 120);
        lv_arc_set_rotation(c->ring_inner, (int)c->ang[1]);
        lv_arc_set_rotation(c->ring_tip[0], (int)c->ang[2]);
        lv_arc_set_rotation(c->ring_tip[1], (int)c->ang[2] + 180);
    }
    if (lv_scr_act() == scr_settings) {
        spin_ang = (spin_ang + 1) % 360;                                  /* slow outer tick */
        lv_arc_set_rotation(g_spin, spin_ang);
    }
}

/* 100 ms: lamp drift + red pulse, key-up bursts, readouts, lock screen extras */
static void lamp_cb(lv_timer_t *t)
{
    static int tick;
    tick++;
    bool keyed = audio_is_keyed(), muted = audio_is_muted();
    bool busy = keyed && !muted;
    console_t *c = active_console();
    if (c) {
        if (keyed && !was_keyed) {                                      /* key-up */
            int n = muted ? 1 : 3;
            for (int k = 0; k < n; k++) lamp_flip(c, rnd() % LAMPS);
            if (!muted) tick_change(c);
        }
        for (int i = 0; i < LAMPS; i++) {
            if (busy && c->lamp_next[i] > 30) c->lamp_next[i] = lamp_delay(true);
            if (--c->lamp_next[i] == 0) { lamp_flip(c, i); c->lamp_next[i] = lamp_delay(busy); }
            if (c->lamp_st[i] == 2) {                                   /* slow red pulse, ~6 s */
                int ph = (tick + i * 8) % 60, tri = ph < 30 ? ph : 60 - ph;
                lv_obj_set_style_bg_opa(c->lamp[i], 120 + tri * 4, 0);
            } else {
                lv_obj_set_style_bg_opa(c->lamp[i], LV_OPA_COVER, 0);
            }
        }
        if (--c->tick_next <= 0) { tick_change(c); c->tick_next = 15 + rnd() % 16; }  /* 1.5-3 s */
        if (tick % 5 == 0) temp_update(c);
    }
    was_keyed = keyed;

    if (lv_scr_act() == con_lock.scr) {
        if (tick % 4 == 0) {
            lv_label_set_text_fmt(lock_hex[0], "%04X", (unsigned)(rnd() & 0xFFFF));
            lv_label_set_text_fmt(lock_hex[1], "%04X", (unsigned)(rnd() & 0xFFFF));
            lv_obj_set_style_text_color(lock_hex[0], COL_AMBER, 0);
            for (int i = 0; i < 5; i++) {
                int h = 12 + rnd() % 30;
                lv_obj_set_height(lock_sig[i], h);
                lv_obj_set_y(lock_sig[i], 414 - h);
            }
        }
        int ph = tick % 20;
        lv_obj_set_style_bg_opa(lock_bar, ph < 10 ? LV_OPA_COVER : LV_OPA_40, 0);
    }
    if (tick % 3 == 0) sensors_readout();
    if (lv_scr_act() == scr_settings && tick % 25 == 0) {                /* settings readout + lamps */
        int i = rnd() % 2;
        lv_label_set_text_static(g_tick[i], WORDS[rnd() % NWORDS]);
        lv_obj_set_style_bg_color(g_lamp[rnd() % 3], C(LAMP_HEX[rnd() % 4]), 0);
    }
}

/* ================= init ================= */
void ui_init(void)
{
    ui_platform_load(&S);
    ui_platform_apply(&S);
    rng_state ^= esp_random();
    if (!rng_state) rng_state = 0x5EED1234;

    skin_main = mk_skin(skin_main_px);
    skin_lock = mk_skin(skin_lock_px);
    skin_set_value = mk_skin(skin_settings_value_px);
    skin_set_wide = mk_skin(skin_settings_wide_px);
    skin_callsign = mk_skin(skin_callsign_px);
    skin_set_sensors = mk_skin(skin_settings_sensors_px);

    f_aureb13 = lv_tiny_ttf_create_data(aurebesh_ttf_start, aurebesh_ttf_len, 14);
    f_aureb12 = lv_tiny_ttf_create_data(aurebesh_ttf_start, aurebesh_ttf_len, 13);
    f_aureng26 = lv_tiny_ttf_create_data(aureng_ttf_start, aureng_ttf_len, 26);
    f_aureng20 = lv_tiny_ttf_create_data(aureng_ttf_start, aureng_ttf_len, 20);
    f_aureng28 = lv_tiny_ttf_create_data(aureng_ttf_start, aureng_ttf_len, 40);   /* settings page */
    f_aureng42 = lv_tiny_ttf_create_data(aureng_ttf_start, aureng_ttf_len, 42);
    f_aureng34 = lv_tiny_ttf_create_data(aureng_ttf_start, aureng_ttf_len, 34);
    f_aureng22 = lv_tiny_ttf_create_data(aureng_ttf_start, aureng_ttf_len, 22);
    f_aureng18 = lv_tiny_ttf_create_data(aureng_ttf_start, aureng_ttf_len, 18);

    build_main();
    build_lock();
    build_settings();
    build_callsign();
    build_blank();

    lv_timer_create(fast_cb, 40, NULL);
    lv_timer_create(ring_cb, 50, NULL);
    lv_timer_create(lamp_cb, 100, NULL);
    lv_scr_load(con_main.scr);
}

#ifdef UI_SIM
/* simulator-only probe for tests: current speed (deg/tick, sign = direction) of a main-screen ring */
__attribute__((export_name("ui_dbg_ring_spd"))) float ui_dbg_ring_spd(int r) { return con_main.spd[r % 3]; }
#endif
