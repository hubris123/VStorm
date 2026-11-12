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
#include <math.h>
#include "lvgl.h"
#include "esp_random.h"
#include "audio.h"
#include "ui.h"
#include "ui_imperial.h"
#include "skin.h"

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

static lv_font_t *f_aureb13, *f_aureb12, *f_aureng26, *f_aureng20, *f_aureng28, *f_aureng42, *f_aureng34, *f_aureng22, *f_aureng18, *f_aureng16, *f_aureng14;
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
static lv_img_dsc_t skin_main, skin_lock, skin_set_value, skin_set_wide, skin_callsign, skin_set_sensors, skin_set_skin;

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
    lv_obj_t *cap, *lbl, *sub, *extra;   /* extra: strip (KEYS_STRIP) or indicator dot (KEYS_PILL) */
    lv_color_t accent;
    uint8_t style;
} cap_t;
static uint8_t cap_style_mode = KEYS_BEVEL;   /* key style used by make_cap (set per skin for MAIN) */

static void cap_style(cap_t *k, bool lit)
{
    lv_obj_t *c = k->cap;
    if (k->style == KEYS_FLAT) {                          /* outlined key, no bevel */
        lv_obj_set_style_border_color(c, k->accent, 0);
        lv_obj_set_style_bg_color(c, lv_color_mix(k->accent, lv_color_black(), 90), 0);
        lv_obj_set_style_bg_grad_color(c, lv_color_mix(k->accent, lv_color_black(), 90), 0);
        lv_obj_set_style_bg_opa(c, lit ? LV_OPA_COVER : LV_OPA_0, 0);
        lv_obj_set_style_shadow_color(c, k->accent, 0);
        lv_obj_set_style_shadow_width(c, lit ? 22 : 0, 0);
        lv_obj_set_style_shadow_opa(c, LV_OPA_70, 0);
        lv_obj_set_style_text_color(k->lbl, lit ? C(0xFFF4F0) : k->accent, 0);
        if (k->sub) lv_obj_set_style_text_color(k->sub, COL_LABEL, 0);
        return;
    }
    if (k->extra && k->style == KEYS_PILL)
        lv_obj_set_style_bg_color(k->extra, lit ? k->accent : lv_color_mix(k->accent, lv_color_black(), 90), 0);
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
    k.style = cap_style_mode;
    bool flat = k.style == KEYS_FLAT;
    int in = flat ? 4 : 11;                               /* flat keys have no housing in the art */
    lv_obj_t *c = lv_obj_create(scr);
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, hx + in, hy + in);
    lv_obj_set_size(c, hw - 2 * in, hh - 2 * in);
    lv_obj_set_style_radius(c, flat ? 8 : 6, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(c, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(c, C(0x050607), 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_translate_y(c, 2, LV_STATE_PRESSED);          /* key travel */
    lv_obj_set_style_bg_color(c, C(0x23292E), LV_STATE_PRESSED);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_ext_click_area(c, in);
    k.cap = c;
    if (k.style == KEYS_STRIP) {                          /* coloured strip across the top of the cap */
        k.extra = box(c, 2, 2, hw - 2 * in - 4, 6, accent);
        lv_obj_set_style_radius(k.extra, 2, 0);
    } else if (k.style == KEYS_PILL) {                    /* indicator dot under the label */
        k.extra = box(c, (hw - 2 * in) / 2 - 8, hh - 2 * in - 14, 16, 8, accent);
        lv_obj_set_style_radius(k.extra, LV_RADIUS_CIRCLE, 0);
    }
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

/* ================= console widgets shared by MAIN and LOCK =================
 * Everything here is driven by the active skin recipe (skin.h): a list of parts (scopes,
 * meters, lamps, readout, rotors, bar graphs, gauges, ...), where, and in what colours. The
 * static art is the skin's background image; these are only the parts that move or change. */
#define MAX_PARTS SKIN_MAX_PARTS
#define MAX_OBJ 40
#define MAX_PTS 480
#define MAX_LAMPS 96
#define MAX_TICK 4
#define MAX_RLINE 13
#define MAX_ARCS 10
#define HIST 128              /* voice level history: 128 x 40 ms = 5 s */

static const char *WORDS[] = {"SECTOR", "ACCESS", "SCANNER", "CHANNEL", "UPLINK", "CIPHER", "RELAY",
                              "VECTOR", "SIGNAL", "BEACON", "ORBIT", "PATROL", "DOCKING", "CLEARED",
                              "ARMED", "COMLINK"};
#define NWORDS (sizeof(WORDS) / sizeof(WORDS[0]))

typedef struct {
    const skin_part_t *d;
    lv_obj_t *o[MAX_OBJ];                 /* segments, bar lines, labels, arcs, rotor lines */
    int no;
    lv_obj_t *chart;
    lv_chart_series_t *ser;
    lv_obj_t *fr[4];                      /* keyed frame overlay (amber muted, red transmitting) */
    int nfr;
    lv_point_t fr_pts[4][9];
    lv_obj_t *needle, *pivot, *marc, *lbl;
    lv_point_t needle_pts[2];
    lv_point_t pts[MAX_PTS];              /* line points: traces, brackets, rotor lines */
    lv_obj_t *canvas;                     /* bar graph: drawn into its own pixel buffer */
    lv_color_t *cbuf;
    int cw, ch;
    lv_obj_t *arcs[MAX_ARCS];
    int narcs;
    float ang[3], spd[3], base[3];        /* rotor: three wandering angles */
    uint16_t rev_next[3];                 /* ticks until each ring reverses */
    float s;                              /* smoothed level; rotors: trail side (+1 / -1, eased) */
    lv_obj_t *blip[3];                    /* radar / reticle blips that drift inside the circle */
    float bx[3], by[3], bh[3];            /* blip offset from the centre + heading (radians) */
    int nblip;
    int shown;                            /* what is drawn now: redraw only on change */
    uint8_t hexv[32];
} part_t;

typedef struct {
    const skin_desc_t *sk;
    lv_obj_t *scr, *callsign, *tx, *clip;
    lv_color_t trace;
    part_t part[MAX_PARTS];
    int np;
    /* lamps (every lamp part feeds one pool) */
    lv_obj_t *lamp[MAX_LAMPS];
    uint8_t lamp_st[MAX_LAMPS], lamp_off[MAX_LAMPS], lamp_line[MAX_LAMPS];
    uint16_t lamp_next[MAX_LAMPS];
    int nlamps;
    lv_point_t dia_pts[18][2];
    int ndia;
    /* readout */
    lv_obj_t *tick[MAX_TICK];
    uint8_t tick_word[MAX_TICK];
    int ntick, tick_next, tick_cx, tick_y0, tick_pitch;
    bool tick_temp;                       /* readout line 0 holds the secret temp */
    /* secret temp */
    part_t *temp_part;
    lv_obj_t *t_word, *t_num, *t_unit;
    int shown_temp;
    /* battery */
    part_t *batt_part;
    lv_obj_t *batt;
    int shown_batt;
    /* tilt crosshair (in the first scope-like part) */
    lv_obj_t *tilt_v, *tilt_h;
    float tilt_x, tilt_y, tilt_opa;
    int tx0, ty0, tx1, ty1;
} console_t;

static console_t con_main, con_lock;

#define CP(hex) lv_color_hex(hex)

/* voice level history (newest = hist_at(0)) */
static uint8_t hist[HIST];
static uint32_t hist_n, fast_ticks;
static void hist_push(int v) { hist[hist_n++ % HIST] = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v); }
static int hist_at(int k) { return hist[(hist_n - 1 - (uint32_t)k) % HIST]; }

/* Callsign in the top bar: as big as fits. 42 px normally; long callsigns step down
 * (34, 26, 22) so they never run into the TX / CLIP lamps (space x 46..242). */
#define CS_MAX_W 196      /* callsign x 46..242; TX label starts ~252 */
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

#define TILT_RANGE 45.0f          /* degrees to reach the scope edge */

static lv_obj_t *pline(lv_obj_t *p, lv_point_t *pts, int n, lv_color_t col, int w)
{
    lv_obj_t *l = lv_line_create(p);
    lv_line_set_points(l, pts, n);
    lv_obj_set_style_line_width(l, w, 0);
    lv_obj_set_style_line_color(l, col, 0);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
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

static lv_obj_t *add_obj(part_t *p, lv_obj_t *o)
{
    if (p->no < MAX_OBJ) p->o[p->no++] = o;
    return o;
}

/* ---- tilt lines: in the first scope-like part ---- */
static void tilt_area(console_t *c, int x0, int y0, int x1, int y1)
{
    if (c->tilt_v) return;
    c->tx0 = x0; c->ty0 = y0; c->tx1 = x1; c->ty1 = y1;
    lv_color_t tcol = lv_color_mix(lv_color_white(), CP(c->sk->grid), 46);   /* a shade whiter than the grid */
    c->tilt_v = box(c->scr, (x0 + x1) / 2, y0, 1, y1 - y0, tcol);
    c->tilt_h = box(c->scr, x0, (y0 + y1) / 2, x1 - x0, 1, tcol);
    c->tilt_opa = 255;
}

/* keyed overlay: same shape as the frame in the art, shown only while the VOX gate is open */
static void make_overlay(console_t *c, part_t *p, int kind, int bx0, int by0, int bx1, int by1)
{
    lv_color_t oc = COL_RED;
    int x0 = bx0 - 3, y0 = by0 - 3, x1 = bx1 + 3, y1 = by1 + 3;
    lv_point_t (*P)[9] = p->fr_pts;
    p->nfr = 0;
    switch (kind) {
    case FRAME_NOTCHED: {
        lv_point_t q[6] = {{x0, y0 + 12}, {x0 + 12, y0}, {x1, y0}, {x1, y1}, {x0, y1}, {x0, y0 + 12}};
        memcpy(P[0], q, sizeof(q));
        p->fr[p->nfr++] = pline(c->scr, P[0], 6, oc, 3);
        break;
    }
    case FRAME_BRACKETS: {
        const int L = 16, cs[4][4] = {{x0, y0, 1, 1}, {x1, y0, -1, 1}, {x0, y1, 1, -1}, {x1, y1, -1, -1}};
        for (int i = 0; i < 4; i++) {
            P[i][0] = (lv_point_t){cs[i][0] + cs[i][2] * L, cs[i][1]};
            P[i][1] = (lv_point_t){cs[i][0], cs[i][1]};
            P[i][2] = (lv_point_t){cs[i][0], cs[i][1] + cs[i][3] * L};
            p->fr[p->nfr++] = pline(c->scr, P[i], 3, oc, 3);
        }
        break;
    }
    case FRAME_DOUBLE:
        for (int j = 0; j < 2; j++) {
            int kk = j ? 5 : 0, cc = 10 - kk;
            lv_point_t q[9] = {{x0 + kk + cc, y0 + kk}, {x1 - kk - cc, y0 + kk}, {x1 - kk, y0 + kk + cc},
                               {x1 - kk, y1 - kk - cc}, {x1 - kk - cc, y1 - kk}, {x0 + kk + cc, y1 - kk},
                               {x0 + kk, y1 - kk - cc}, {x0 + kk, y0 + kk + cc}, {x0 + kk + cc, y0 + kk}};
            memcpy(P[j], q, sizeof(q));
            p->fr[p->nfr++] = pline(c->scr, P[j], 9, oc, j ? 1 : 3);
        }
        break;
    default: {                                             /* ruler-tick / dashed: solid outline */
        lv_point_t q[5] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, {x0, y0}};
        memcpy(P[0], q, sizeof(q));
        p->fr[p->nfr++] = pline(c->scr, P[0], 5, oc, 3);
    }
    }
    for (int i = 0; i < p->nfr; i++) lv_obj_add_flag(p->fr[i], LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *make_chart(console_t *c, int x, int y, int w, int h, int pad, int npts, int ymax, lv_color_t col,
                            lv_chart_series_t **ser)
{
    lv_obj_t *chrt = lv_chart_create(c->scr);
    lv_obj_set_pos(chrt, x, y);
    lv_obj_set_size(chrt, w, h);
    lv_obj_set_style_bg_opa(chrt, LV_OPA_0, 0);            /* panel + grid are in the skin */
    lv_obj_set_style_border_width(chrt, 0, 0);
    lv_obj_set_style_radius(chrt, 0, 0);
    lv_obj_set_style_pad_all(chrt, pad, 0);
    lv_chart_set_div_line_count(chrt, 0, 0);
    lv_chart_set_type(chrt, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chrt, npts);
    lv_chart_set_range(chrt, LV_CHART_AXIS_PRIMARY_Y, 0, ymax);
    lv_chart_set_update_mode(chrt, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_size(chrt, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chrt, 2, LV_PART_ITEMS);
    lv_obj_clear_flag(chrt, LV_OBJ_FLAG_CLICKABLE);
    *ser = lv_chart_add_series(chrt, col, LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < npts; i++) lv_chart_set_next_value(chrt, *ser, 3);
    return chrt;
}

/* ---- scopes ---- */
static void make_scope(console_t *c, part_t *p)
{
    const int16_t *g = p->d->g;
    if (p->d->kind == PK_ROUND) {
        int x = g[0], y = g[1], r = g[2];
        tilt_area(c, x - r * 3 / 4, y - r * 3 / 4, x + r * 3 / 4, y + r * 3 / 4);
        p->chart = make_chart(c, x - r * 8 / 10, y - r * 55 / 100, r * 16 / 10, r * 90 / 100, 0, 50, 100, c->trace, &p->ser);
        p->fr[0] = ring(c->scr, x, y, r + 1, 3, 360, COL_RED);
        lv_obj_add_flag(p->fr[0], LV_OBJ_FLAG_HIDDEN);
        p->nfr = 1;
        return;
    }
    tilt_area(c, g[0] + 3, g[1] + 3, g[2] - 3, g[3] - 3);
    p->chart = make_chart(c, g[0] + 1, g[1] + 1, g[2] - g[0] - 1, g[3] - g[1] - 1, 4, 60, 100, c->trace, &p->ser);
    make_overlay(c, p, p->d->style, g[0], g[1], g[2], g[3]);
}

/* tall scope: the trace runs down the panel, newest at the bottom, level to the right */
static void vscope_update(part_t *p)
{
    const int16_t *g = p->d->g;
    for (int i = 0; i < 60; i++) {
        int v = hist_at(59 - i);
        p->pts[i] = (lv_point_t){g[0] + 4 + (g[2] - g[0] - 8) * v / 100, g[1] + 3 + (g[3] - g[1] - 6) * i / 59};
    }
    lv_line_set_points(p->o[0], p->pts, 60);
}

static void make_vscope(console_t *c, part_t *p)
{
    const int16_t *g = p->d->g;
    tilt_area(c, g[0] + 3, g[1] + 3, g[2] - 3, g[3] - 3);
    for (int i = 0; i < 60; i++) p->pts[i] = (lv_point_t){g[0] + 6, g[1] + 3 + (g[3] - g[1] - 6) * i / 59};
    add_obj(p, pline(c->scr, p->pts, 60, c->trace, 2));
    make_overlay(c, p, p->d->style, g[0], g[1], g[2], g[3]);
}

/* target view: trace in the lower part, four brackets that close in while talking */
static void target_brackets(part_t *p)
{
    const int16_t *g = p->d->g;
    int cx = (g[0] + g[2]) / 2, cy = (g[1] + g[3]) / 2;
    int w2 = 46 - (int)(16 * p->s / 100), h2 = 30 - (int)(10 * p->s / 100);
    if (w2 == p->shown) return;
    p->shown = w2;
    static const int8_t SG[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    for (int i = 0; i < 4; i++) {
        int ex = cx + SG[i][0] * w2, ey = cy + SG[i][1] * h2;
        lv_point_t *P = &p->pts[i * 3];
        P[0] = (lv_point_t){ex - SG[i][0] * 14, ey};
        P[1] = (lv_point_t){ex, ey};
        P[2] = (lv_point_t){ex, ey - SG[i][1] * 12};
        lv_line_set_points(p->o[i], P, 3);
    }
}

static void make_target(console_t *c, part_t *p)
{
    const int16_t *g = p->d->g;
    tilt_area(c, g[0] + 3, g[1] + 3, g[2] - 3, g[3] - 3);
    p->chart = make_chart(c, g[0] + 3, g[1] + 6, g[2] - g[0] - 6, g[3] - g[1] - 12, 0, 60, 166,
                          lv_color_mix(c->trace, lv_color_black(), 178), &p->ser);
    for (int i = 0; i < 4; i++) add_obj(p, pline(c->scr, &p->pts[i * 3], 3, CP(c->sk->accent), 3));
    p->shown = -1;
    target_brackets(p);
    make_overlay(c, p, FRAME_BRACKETS, g[0], g[1], g[2], g[3]);
}

/* waterfall: rows of past traces stacked, newest (brightest) at the bottom */
static void waterfall_update(part_t *p)
{
    const int16_t *g = p->d->g;
    int rows = g[4], W = g[2] - g[0] - 6;
    float pitch = (float)(g[3] - g[1] - 16) / rows;
    for (int r = 0; r < rows; r++) {
        float amp = 0.25f + 0.75f * r / (rows - 1), base = g[1] + 14 + r * pitch;
        int lag = (rows - 1 - r) * 8;
        lv_point_t *P = &p->pts[r * 50];
        for (int i = 0; i < 50; i++) {
            int v = hist_at(lag + 49 - i);
            P[i] = (lv_point_t){g[0] + 3 + W * i / 49, (lv_coord_t)(base - v / 100.0f * pitch * 1.6f * amp)};
        }
        lv_line_set_points(p->o[r], P, 50);
    }
}

static void make_waterfall(console_t *c, part_t *p)
{
    const int16_t *g = p->d->g;
    int rows = g[4] > 9 ? 9 : g[4];
    tilt_area(c, g[0] + 3, g[1] + 3, g[2] - 3, g[3] - 3);
    for (int r = 0; r < rows; r++) {
        lv_color_t col = lv_color_mix(c->trace, lv_color_black(), (uint8_t)(64 + 191 * r / (rows - 1)));
        for (int i = 0; i < 50; i++) p->pts[r * 50 + i] = (lv_point_t){g[0] + 3, g[1] + 14};
        add_obj(p, pline(c->scr, &p->pts[r * 50], 50, col, r == rows - 1 ? 2 : 1));
    }
    waterfall_update(p);
    make_overlay(c, p, FRAME_NOTCHED, g[0], g[1], g[2], g[3]);
}

static void make_small_lamps(console_t *c)
{
    c->tx = box(c->scr, 276, 22, 14, 14, COL_PANEL);      /* TX/CLIP: CLIP right edge = battery bar (354) */
    c->clip = box(c->scr, 340, 22, 14, 14, COL_PANEL);
    lv_obj_set_style_border_width(c->tx, 1, 0);
    lv_obj_set_style_border_color(c->tx, CP(c->sk->frame), 0);
    lv_obj_set_style_border_width(c->clip, 1, 0);
    lv_obj_set_style_border_color(c->clip, CP(c->sk->frame), 0);
}

/* ---- voice meters ---- */
static void make_meter(console_t *c, part_t *p)
{
    const skin_desc_t *k = c->sk;
    const int16_t *m = p->d->g;
    switch (p->d->style) {
    case METER_VSEG: {
        int n = m[4] > MAX_OBJ ? MAX_OBJ : m[4];
        float h = (float)(m[2] - m[1]) / n;
        for (int i = 0; i < n; i++)
            add_obj(p, box(c->scr, m[0], (int)(m[2] - (i + 1) * h + 2), m[3], (int)(h - 3), COL_GREEN));
        break;
    }
    case METER_DOTS: {
        int n = m[3] > MAX_OBJ ? MAX_OBJ : m[3];
        float step = (float)(m[2] - m[1]) / n, r = step * 0.36f;
        for (int i = 0; i < n; i++) {
            lv_obj_t *o = box(c->scr, (int)(m[0] - r), (int)(m[2] - step * (i + 0.5f) - r), (int)(2 * r), (int)(2 * r), COL_GREEN);
            lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
            add_obj(p, o);
        }
        break;
    }
    case METER_TWIN:
        for (int i = 0; i < 2; i++) {
            lv_obj_t *o = box(c->scr, m[0] + i * 16, m[2] - 1, 10, 1, CP(k->accent));
            lv_obj_set_style_bg_grad_color(o, CP(k->trace), 0);
            lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
            add_obj(p, o);
        }
        break;
    case METER_HSEG: {
        int n = m[4] > MAX_OBJ ? MAX_OBJ : m[4];
        float w = (float)(m[1] - m[0]) / n;
        for (int i = 0; i < n; i++)
            add_obj(p, box(c->scr, (int)(m[0] + i * w + 1), m[2], (int)(w - 3), m[3], CP(k->trace)));
        break;
    }
    case METER_ARC:
        p->marc = ring(c->scr, m[0], m[1], m[2] + 4, 8, 0, CP(k->trace));
        lv_arc_set_rotation(p->marc, 200);
        p->needle_pts[0] = (lv_point_t){m[0], m[1]};
        p->needle_pts[1] = (lv_point_t){m[0], m[1]};
        p->needle = pline(c->scr, p->needle_pts, 2, CP(k->accent), 2);
        p->pivot = box(c->scr, m[0] - 4, m[1] - 4, 8, 8, CP(k->frame));
        lv_obj_set_style_radius(p->pivot, LV_RADIUS_CIRCLE, 0);
        break;
    }
}

static void meter_update(console_t *c, part_t *p, int lvl)
{
    const skin_desc_t *k = c->sk;
    const int16_t *m = p->d->g;
    switch (p->d->style) {
    case METER_TWIN:
        for (int i = 0; i < 2; i++) {
            int lv = i ? (lvl > 12 ? lvl - 12 : 0) : lvl;
            int h = (m[2] - m[1]) * lv / 100;
            if (h < 1) h = 1;
            lv_obj_set_y(p->o[i], m[2] - h);
            lv_obj_set_height(p->o[i], h);
        }
        break;
    case METER_ARC: {
        lv_arc_set_angles(p->marc, 0, 140 * lvl / 100);
        float a = (200.0f + 140.0f * lvl / 100.0f) * 3.14159265f / 180.0f;
        p->needle_pts[1] = (lv_point_t){m[0] + (int)((m[2] - 6) * cosf(a)), m[1] + (int)((m[2] - 6) * sinf(a))};
        lv_line_set_points(p->needle, p->needle_pts, 2);
        break;
    }
    default: {
        int n = p->no, lit = lvl * n / 100;
        if (lit == p->shown) return;                      /* unchanged: no redraw */
        p->shown = lit;
        lv_color_t lo = p->d->style == METER_VSEG ? COL_GREEN : CP(k->trace);
        for (int i = 0; i < n; i++) {
            lv_obj_set_style_bg_opa(p->o[i], i < lit ? LV_OPA_COVER : LV_OPA_0, 0);
            lv_obj_set_style_bg_color(p->o[i], i >= n * 3 / 4 ? CP(k->accent) : lo, 0);
        }
    }
    }
}

/* ---- bar graph: segmented bars of recent voice level, drawn into a small canvas ---- */
#define BAR_SEG 5
static void bars_fill(part_t *p, int x0, int y0, int x1, int y1, lv_color_t col)
{
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) p->cbuf[y * p->cw + x] = col;
}

static void bars_update(console_t *c, part_t *p)
{
    int n = p->d->g[4], W = p->cw, H = p->ch;
    float bw = (float)(W - 8) / n;
    int nseg = (H - 8) / BAR_SEG;
    lv_color_t bg = CP(0x06080A), off = CP(0x0E1215), lo = CP(c->sk->trace), hi = CP(c->sk->accent);
    bars_fill(p, 0, 0, W, H, bg);
    for (int i = 0; i < n; i++) {
        int v = hist_at((n - 1 - i) * 3);
        int lit = nseg * v / 100;
        int bx0 = (int)(4 + i * bw + 1), bx1 = (int)(4 + (i + 1) * bw - 1);
        for (int k = 0; k < nseg; k++) {
            int yb = H - 4 - k * BAR_SEG;
            lv_color_t col = k < lit ? (k > nseg * 78 / 100 ? hi : lo) : off;
            bars_fill(p, bx0, yb - BAR_SEG + 1, bx1, yb, col);
        }
    }
    lv_obj_invalidate(p->canvas);
}

static void make_bars(console_t *c, part_t *p)
{
    const int16_t *g = p->d->g;
    p->cw = g[2] - g[0];
    p->ch = g[3] - g[1];
    p->cbuf = lv_mem_alloc(p->cw * p->ch * sizeof(lv_color_t));
    if (!p->cbuf) return;
    p->canvas = lv_canvas_create(c->scr);
    lv_canvas_set_buffer(p->canvas, p->cbuf, p->cw, p->ch, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(p->canvas, g[0], g[1]);
    lv_obj_clear_flag(p->canvas, LV_OBJ_FLAG_CLICKABLE);
    bars_update(c, p);
}

/* ---- ring meter: segments around a rotor light up clockwise with the voice ---- */
static void make_ringmeter(console_t *c, part_t *p)
{
    const int16_t *g = p->d->g;
    int n = g[3] > MAX_OBJ ? MAX_OBJ : g[3];
    float step = 300.0f / n;
    for (int i = 0; i < n; i++) {
        lv_obj_t *a = ring(c->scr, g[0], g[1], g[2], g[4], (int)(step - 4),
                           i > n * 8 / 10 ? CP(c->sk->accent) : CP(c->sk->trace));
        lv_arc_set_rotation(a, (int)(120 + i * step));
        lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);
        add_obj(p, a);
    }
    p->shown = 0;
}

static void ringmeter_update(part_t *p, int lvl)
{
    p->s += (lvl - p->s) * 0.4f;
    int lit = (int)(p->s * p->no / 100 + 0.5f);
    if (lit == p->shown) return;
    for (int i = 0; i < p->no; i++) {
        bool on = i < lit;
        if (on != (i < p->shown)) {
            if (on) lv_obj_clear_flag(p->o[i], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(p->o[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    p->shown = lit;
}

/* ---- big gauge: lit arc + needle; the scale is in the art ---- */
static void make_gauge(console_t *c, part_t *p)
{
    const int16_t *g = p->d->g;
    p->marc = ring(c->scr, g[0], g[1], g[2], 14, 0, CP(c->sk->trace));
    lv_arc_set_rotation(p->marc, 180);
    p->needle_pts[0] = p->needle_pts[1] = (lv_point_t){g[0], g[1]};
    p->needle = pline(c->scr, p->needle_pts, 2, CP(c->sk->accent), 3);
    lv_obj_set_style_line_rounded(p->needle, true, 0);
    p->pivot = box(c->scr, g[0] - 9, g[1] - 9, 18, 18, CP(c->sk->frame));
    lv_obj_set_style_radius(p->pivot, LV_RADIUS_CIRCLE, 0);
    p->shown = -1;
}

static void gauge_update(part_t *p, int lvl)
{
    const int16_t *g = p->d->g;
    p->s += (lvl - p->s) * 0.35f;                          /* a needle has some weight */
    int v = (int)(p->s + 0.5f);
    if (v == p->shown) return;
    p->shown = v;
    lv_arc_set_angles(p->marc, 0, 180 * v / 100);
    float a = (180.0f + 180.0f * v / 100.0f) * 3.14159265f / 180.0f;
    p->needle_pts[1] = (lv_point_t){g[0] + (int)((g[2] - 22) * cosf(a)), g[1] + (int)((g[2] - 22) * sinf(a))};
    lv_line_set_points(p->needle, p->needle_pts, 2);
}

/* ---- hex table: readings that change now and then (one cell can hold the temp) ---- */
static void hex_set(console_t *c, part_t *p, int i, int v)
{
    p->hexv[i] = (uint8_t)v;
    lv_label_set_text_fmt(p->o[i], "%02X", v);
}

static void make_hex(console_t *c, part_t *p)
{
    const int16_t *g = p->d->g;
    int n = g[2] * g[3];
    if (n > 32) n = 32;
    for (int i = 0; i < n; i++) {
        int col = i % g[2], row = i / g[2];
        lv_color_t tc = (row + col) % 3 ? CP(c->sk->tick) : lv_color_mix(lv_color_white(), CP(c->sk->tick), 64);
        lv_obj_t *l = add_obj(p, label(c->scr, "00", &lv_font_montserrat_14, tc));
        lv_obj_set_pos(l, g[0] + col * g[4], g[1] + row * g[5] - 8);   /* g = left, middle of the row */
        hex_set(c, p, i, rnd() & 0xFF);
    }
}

static void hex_tick(console_t *c, part_t *p)
{
    int i = rnd() % p->no;
    if (c->temp_part == p && i == p->d->style && S.show_temp) return;   /* the temp cell stays */
    hex_set(c, p, i, rnd() & 0xFF);
}

/* ---- battery: drains from the top (vertical) or the right (horizontal);
 *      green, yellow <= 30 %, red <= 15 % ---- */
static void make_batt(console_t *c, part_t *p)
{
    const int16_t *b = p->d->g;
    if (c->batt) return;                                   /* one battery per skin */
    c->batt_part = p;
    if (p->d->style) c->batt = box(c->scr, b[0], b[2], b[1] - b[0], b[3], COL_GREEN);
    else c->batt = box(c->scr, b[0], b[1], b[3], b[2] - b[1], COL_GREEN);
    c->shown_batt = -999;
}

/* Slow sensors are read rarely and cached: on the device each read is an I2C transaction,
 * and neither battery charge nor board temperature changes quickly. Opening the Sensors
 * page or flipping a toggle forces a fresh read. Tilt is read every frame (it animates). */
#define BATT_PERIOD_MS 10000
#define TEMP_PERIOD_MS 30000
static int batt_cache = -1;
static float temp_cache;
static bool batt_valid, temp_valid, temp_ok;
static uint32_t batt_t, temp_t;

static void sensors_invalidate(void) { batt_valid = temp_valid = false; }

static int batt_get(void)
{
    if (!batt_valid || lv_tick_elaps(batt_t) >= BATT_PERIOD_MS) {
        batt_cache = ui_platform_battery();
        batt_t = lv_tick_get();
        batt_valid = true;
    }
    return batt_cache;
}

static bool temp_get(float *c)
{
    if (!temp_valid || lv_tick_elaps(temp_t) >= TEMP_PERIOD_MS) {
        temp_ok = ui_platform_temp(&temp_cache);
        temp_t = lv_tick_get();
        temp_valid = true;
    }
    *c = temp_cache;
    return temp_ok;
}

static void batt_update(console_t *c)
{
    if (!c->batt) return;
    int pct = S.show_batt ? batt_get() : -1;
    if (pct == c->shown_batt) return;                     /* nothing changed: no redraw */
    c->shown_batt = pct;
    if (pct < 0) { lv_obj_add_flag(c->batt, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_clear_flag(c->batt, LV_OBJ_FLAG_HIDDEN);
    if (pct > 100) pct = 100;
    const int16_t *b = c->batt_part->d->g;
    if (c->batt_part->d->style) {
        int w = (b[1] - b[0]) * pct / 100;
        lv_obj_set_width(c->batt, w < 2 ? 2 : w);
    } else {
        int h = (b[2] - b[1]) * pct / 100;
        if (h < 2) h = 2;
        lv_obj_set_y(c->batt, b[2] - h);
        lv_obj_set_height(c->batt, h);
    }
    lv_obj_set_style_bg_color(c->batt, pct <= 15 ? COL_RED : pct <= 30 ? COL_AMBER : COL_GREEN, 0);
}

/* tilt lines glide toward the sensor angle; fade out while talking if QUIET ONLY */
static void tilt_update(console_t *c, bool keyed)
{
    if (!c->tilt_v) return;
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
    int cx = (c->tx0 + c->tx1) / 2 + (int)(fx * (c->tx1 - c->tx0) / 2);
    int cy = (c->ty0 + c->ty1) / 2 + (int)(fy * (c->ty1 - c->ty0) / 2);
    if (cx > c->tx1 - 1) cx = c->tx1 - 1;
    if (cy > c->ty1 - 1) cy = c->ty1 - 1;
    lv_obj_set_x(c->tilt_v, cx);
    lv_obj_set_y(c->tilt_h, cy);
    lv_opa_t o = (lv_opa_t)(c->tilt_opa < 2 ? 0 : c->tilt_opa);
    lv_obj_set_style_bg_opa(c->tilt_v, o, 0);
    lv_obj_set_style_bg_opa(c->tilt_h, o, 0);
}

/* ---- lamps ---- */
static uint16_t lamp_delay(bool busy)
{
    return busy ? 10 + rnd() % 21          /* 1-3 s while transmitting */
                : 200 + rnd() % 201;       /* 20-40 s idle */
}

static lv_color_t lamp_color(console_t *c, int i)
{
    int st = c->lamp_st[i];
    if (c->lamp_off[i] && st == 0) return CP(0x151A1F);
    return CP(c->sk->lamp[st & 3]);
}

/* lamps are boxes, except diamonds, which are thick 45-degree line segments: a segment
 * as long as it is wide is a square turned 45 degrees (no rotation feature needed) */
static void lamp_paint(console_t *c, int i, lv_color_t col, lv_opa_t opa)
{
    if (c->lamp_line[i]) {
        lv_obj_set_style_line_color(c->lamp[i], col, 0);
        lv_obj_set_style_line_opa(c->lamp[i], opa, 0);
    } else {
        lv_obj_set_style_bg_color(c->lamp[i], col, 0);
        lv_obj_set_style_bg_opa(c->lamp[i], opa, 0);
    }
}

static int lamp_register(console_t *c, lv_obj_t *o, bool has_off, bool line)
{
    int i = c->nlamps++;
    c->lamp[i] = o;
    c->lamp_off[i] = has_off;
    c->lamp_line[i] = line;
    c->lamp_st[i] = rnd() % 4;
    c->lamp_next[i] = lamp_delay(false);
    lamp_paint(c, i, lamp_color(c, i), LV_OPA_COVER);
    return i;
}

static lv_obj_t *add_lamp(console_t *c, int x, int y, int w, int h, bool dome, bool has_off)
{
    if (c->nlamps >= MAX_LAMPS) return NULL;
    lv_obj_t *o = box(c->scr, x, y, w, h, COL_PANEL);
    if (dome) {
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        lv_obj_t *hl = box(o, w / 5, h / 5, w / 4, h / 5, lv_color_white());   /* dome highlight */
        lv_obj_set_style_radius(hl, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(hl, LV_OPA_40, 0);
    }
    lamp_register(c, o, has_off, false);
    return o;
}

static void make_lamps(console_t *c, part_t *p)
{
    const int16_t *g = p->d->g;
    int cx = g[0], cy = g[1];
    switch (p->d->style) {
    case LAMPS_GRID: {
        /* 4-row grid, 29 px pitch, row centres 7/36/65/94 (readout lines sit on the same rows) */
        static const struct { int x, y, w, h; bool dome; } L[7] = {
            {0, 0, 80, 14, false}, {0, 29, 32, 14, false}, {42, 22, 28, 28, true},
            {0, 58, 32, 14, false}, {42, 58, 32, 14, false}, {0, 80, 28, 28, true}, {42, 87, 32, 14, false},
        };
        for (int i = 0; i < 7; i++) add_lamp(c, cx - 40 + L[i].x, cy - 54 + L[i].y, L[i].w, L[i].h, L[i].dome, false);
        break;
    }
    case LAMPS_SWITCHER: {                                 /* broadcast-switcher bank */
        int cols = g[2] ? g[2] : 8, rows = g[3] ? g[3] : 5;
        int x0 = cx - (cols * 13 - 4) / 2, y0 = cy - (rows * 14 - 7) / 2;
        for (int r = 0; r < rows; r++)
            for (int col = 0; col < cols; col++) add_lamp(c, x0 + col * 13, y0 + r * 14, 9, 7, false, true);
        break;
    }
    case LAMPS_DOMES: {
        int cols = g[2] ? g[2] : 3, rows = g[3] ? g[3] : 3, r = g[4] ? g[4] : 11, pitch = r * 2 + 8;
        int x0 = cx - (cols - 1) * pitch / 2, y0 = cy - (rows - 1) * pitch / 2;
        for (int i = 0; i < cols * rows; i++)
            add_lamp(c, x0 + (i % cols) * pitch - r, y0 + (i / cols) * pitch - r, 2 * r, 2 * r, true, false);
        break;
    }
    case LAMPS_PILLS: {                                    /* lit part of each tube (tube is in the art) */
        static const int H[5] = {34, 52, 70, 34, 52};
        for (int i = 0; i < 5; i++) {
            lv_obj_t *o = add_lamp(c, cx - 32 + i * 16 - 5, cy + 46 - H[i], 10, H[i], false, false);
            if (o) lv_obj_set_style_radius(o, 5, 0);
        }
        break;
    }
    case LAMPS_DIAMONDS:
        for (int i = 0; i < 9 && c->ndia < 18 && c->nlamps < MAX_LAMPS; i++) {
            int x = cx + ((i % 3) - 1) * 26 + (((i / 3) % 2) ? 13 : 0) - 6, y = cy + ((i / 3) - 1) * 28;
            /* segment (x-5,y-5)-(x+5,y+5): 14 px long, 14 px wide = diamond of half-width 10 */
            lv_point_t *P = c->dia_pts[c->ndia++];
            P[0] = (lv_point_t){x - 5, y - 5};
            P[1] = (lv_point_t){x + 5, y + 5};
            lv_obj_t *o = pline(c->scr, P, 2, COL_PANEL, 14);
            lv_obj_set_style_line_rounded(o, false, 0);
            lamp_register(c, o, false, true);
        }
        break;
    case LAMPS_MATRIX: {
        int x0 = cx - (4 * 22 - 6) / 2, y0 = cy - (4 * 22 - 6) / 2;
        for (int i = 0; i < 16; i++) add_lamp(c, x0 + (i % 4) * 22, y0 + (i / 4) * 22, 16, 16, false, true);
        break;
    }
    }
}

/* edge strip: a column of small lamps down the side of the console */
static void make_edge(console_t *c, part_t *p)
{
    const int16_t *g = p->d->g;
    for (int y = g[1]; y + 7 <= g[2]; y += 12) add_lamp(c, g[0], y, g[3], 7, false, true);
}

static void lamp_flip(console_t *c, int i)
{
    c->lamp_st[i] = (c->lamp_st[i] + 1 + rnd() % 3) % 4;     /* always a different colour */
    lamp_paint(c, i, lamp_color(c, i), LV_OPA_COVER);
}

/* ---- readout (Aurebesh lines; line 0 can hold the secret temp) ---- */
static void tick_place(console_t *c, int i)
{
    lv_obj_update_layout(c->tick[i]);
    lv_obj_set_pos(c->tick[i], c->tick_cx - lv_obj_get_width(c->tick[i]) / 2,
                   c->tick_y0 + i * c->tick_pitch - lv_obj_get_height(c->tick[i]) / 2);
}

static int temp_key(bool *have)
{
    float tc = 0;
    *have = S.show_temp && temp_get(&tc);
    if (!*have) return -100000;
    float v = S.temp_f ? tc * 9.0f / 5.0f + 32.0f : tc;
    int iv = (int)(v + (v < 0 ? -0.5f : 0.5f));
    return iv * 2 + (S.temp_f ? 1 : 0);
}

/* the secret temperature: on readout line 0 ("TEMP" + digits + unit), or hidden in another
 * part: a gauge plate, a tag, a hex cell (in hex: 88 F reads 58) */
static void temp_update(console_t *c)
{
    if (!c->tick_temp && !c->temp_part) return;
    bool have;
    int key = temp_key(&have);
    if (key == c->shown_temp) return;                     /* unchanged: no redraw */
    c->shown_temp = key;
    int val = have ? (key - (S.temp_f ? 1 : 0)) / 2 : 0;
    if (c->temp_part) {
        part_t *p = c->temp_part;
        const int16_t *g = p->d->g;
        if (p->d->kind == PK_HEX) {
            int cell = p->d->style;
            if (cell < p->no) hex_set(c, p, cell, have ? (val < 0 ? 0 : val > 255 ? 255 : val) : rnd() & 0xFF);
            return;
        }
        if (!have) { lv_obj_add_flag(p->lbl, LV_OBJ_FLAG_HIDDEN); return; }
        lv_obj_clear_flag(p->lbl, LV_OBJ_FLAG_HIDDEN);
        static const char *PFX[3] = {"", "RNG ", "CH "};
        lv_label_set_text_fmt(p->lbl, "%s%d", PFX[p->d->style % 3], val);
        lv_obj_update_layout(p->lbl);
        int w = lv_obj_get_width(p->lbl), h = lv_obj_get_height(p->lbl);
        if (p->d->kind == PK_GAUGE) { lv_obj_set_pos(p->lbl, g[0] - w / 2, g[1] + 19 - h / 2); return; }
        int x = g[2] == 1 ? g[0] - w : g[2] == 2 ? g[0] : g[0] - w / 2;
        lv_obj_set_pos(p->lbl, x, g[1] - h / 2);
        return;
    }
    if (!have) {
        lv_obj_add_flag(c->t_word, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(c->t_num, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(c->t_unit, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(c->tick[0], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(c->tick[0], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(c->t_word, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(c->t_num, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(c->t_unit, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(c->t_num, "%d", val);
    lv_label_set_text_static(c->t_unit, S.temp_f ? "F" : "C");
    lv_obj_update_layout(c->t_word);
    int w1 = lv_obj_get_width(c->t_word), w2 = lv_obj_get_width(c->t_num), w3 = lv_obj_get_width(c->t_unit);
    int gap = 6, x = c->tick_cx - (w1 + gap + w2 + 4 + w3) / 2, cy = c->tick_y0;
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

/* temp hidden in a tag or on a gauge plate: TK-style digits (or small text with a prefix) */
static void make_temp_label(console_t *c, part_t *p)
{
    const lv_font_t *f;
    if (p->d->kind == PK_GAUGE) f = f_aureng18 ? f_aureng18 : &lv_font_montserrat_18;
    else if (p->d->style) f = p->d->g[3] <= 13 ? &lv_font_montserrat_12 : &lv_font_montserrat_14;
    else f = p->d->g[3] <= 14 ? (f_aureng14 ? f_aureng14 : &lv_font_montserrat_14)
                              : (f_aureng16 ? f_aureng16 : &lv_font_montserrat_16);
    p->lbl = label(c->scr, "", f, CP(c->sk->tick));
    lv_obj_add_flag(p->lbl, LV_OBJ_FLAG_HIDDEN);
    c->temp_part = p;
}

static void make_ticker(console_t *c, part_t *p)
{
    const int16_t *g = p->d->g;
    lv_color_t col = CP(c->sk->tick);
    if (c->ntick) return;                                  /* one readout per skin */
    c->ntick = g[2] > MAX_TICK ? MAX_TICK : g[2];
    c->tick_cx = g[0];
    c->tick_pitch = g[3];
    c->tick_y0 = g[1] - ((c->ntick - 1) * c->tick_pitch) / 2;
    c->tick_temp = (p->d->flags & PF_TEMP) != 0;
    for (int i = 0; i < c->ntick; i++) {
        c->tick_word[i] = (i * 5 + (c == &con_lock ? 3 : 0)) % NWORDS;
        c->tick[i] = aureb_label(c->scr, WORDS[c->tick_word[i]], f_aureb13, col);
        tick_place(c, i);
    }
    c->tick_next = 8;
    if (!c->tick_temp) return;
    c->t_word = aureb_label(c->scr, "TEMP", f_aureb13, col);
    c->t_num = label(c->scr, "0", f_aureng20 ? f_aureng20 : &lv_font_montserrat_16, col);   /* 20 px: digits 13 px tall = the Aurebesh letters as drawn */
    c->t_unit = aureb_label(c->scr, "F", f_aureb13, col);
    lv_obj_add_flag(c->t_word, LV_OBJ_FLAG_HIDDEN);        /* hidden until temp_update places them */
    lv_obj_add_flag(c->t_num, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(c->t_unit, LV_OBJ_FLAG_HIDDEN);
}

static void tick_change(console_t *c)
{
    int first = (c->tick_temp && S.show_temp) ? 1 : 0;     /* line 0 holds the temp readout */
    if (c->ntick - first < 1) return;
    int i = first + rnd() % (c->ntick - first), w;
    for (;;) {
        w = rnd() % NWORDS;
        bool used = false;
        for (int k = 0; k < c->ntick; k++) used |= (c->tick_word[k] == w);
        if (!used) break;
    }
    c->tick_word[i] = w;
    lv_label_set_text_static(c->tick[i], WORDS[w]);
    tick_place(c, i);
}

/* ---- rotors: three independently wandering angles drive every design ---- */
static void rotor_lines_square(part_t *p)
{
    const int16_t *t = p->d->g;
    for (int s = 0; s < 3; s++) {
        float rr = t[2] - s * 13, a0 = (p->ang[s] + 45) * 3.14159265f / 180.0f;
        float px[4], py[4];
        for (int i = 0; i < 4; i++) {
            px[i] = t[0] + rr * cosf(a0 + 1.5707963f * i);
            py[i] = t[1] + rr * sinf(a0 + 1.5707963f * i);
        }
        for (int i = 0; i < 4; i++) {
            int pv = (i + 3) % 4, nx = (i + 1) % 4;
            lv_point_t *P = &p->pts[(s * 4 + i) * 34];
            P[0] = (lv_point_t){(int)(px[i] + (px[pv] - px[i]) * 0.3f), (int)(py[i] + (py[pv] - py[i]) * 0.3f)};
            P[1] = (lv_point_t){(int)px[i], (int)py[i]};
            P[2] = (lv_point_t){(int)(px[i] + (px[nx] - px[i]) * 0.3f), (int)(py[i] + (py[nx] - py[i]) * 0.3f)};
            lv_line_set_points(p->o[s * 4 + i], P, 3);
        }
    }
}

static void rotor_gear_pts(part_t *p)
{
    const int16_t *t = p->d->g;
    float o = p->ang[0] * 3.14159265f / 180.0f;
    for (int i = 0; i < 33; i++) {
        float a = o + 3.14159265f * i / 16.0f, rr = (i % 2 == 0) ? t[2] : t[2] - 7;
        p->pts[i] = (lv_point_t){(int)(t[0] + rr * cosf(a)), (int)(t[1] + rr * sinf(a))};
    }
    lv_line_set_points(p->o[0], p->pts, 33);
}

static void sweep_line(part_t *p, int idx, float deg, float len)
{
    const int16_t *t = p->d->g;
    float a = deg * 3.14159265f / 180.0f;
    lv_point_t *P = &p->pts[idx * 34];
    P[0] = (lv_point_t){t[0], t[1]};
    P[1] = (lv_point_t){(int)(t[0] + len * cosf(a)), (int)(t[1] + len * sinf(a))};
    lv_line_set_points(p->o[idx], P, 2);
}

static void rotor_draw(part_t *p)
{
    const int16_t *g = p->d->g;
    switch (p->d->style) {
    case ROTOR_SCOMP:
        for (int i = 0; i < 3; i++) lv_arc_set_rotation(p->arcs[i], (int)p->ang[0] + i * 120);
        lv_arc_set_rotation(p->arcs[3], (int)p->ang[1]);
        lv_arc_set_rotation(p->arcs[4], (int)p->ang[2]);
        lv_arc_set_rotation(p->arcs[5], (int)p->ang[2] + 180);
        break;
    case ROTOR_RETICLE:                                    /* the trail always follows the arm */
        for (int k = 0; k < 3; k++) sweep_line(p, k, p->ang[0] - (3 - k) * 6 * p->s, g[2]);
        sweep_line(p, 3, p->ang[0], g[2]);
        lv_arc_set_rotation(p->arcs[0], (int)p->ang[1]);
        break;
    case ROTOR_SQUARE:
        rotor_lines_square(p);
        break;
    case ROTOR_TWIN:
        for (int s = 0; s < 2; s++) {
            int sg = s ? -1 : 1;
            for (int i = 0; i < 3; i++) lv_arc_set_rotation(p->arcs[s * 4 + i], (int)(sg * p->ang[0]) + i * 120);
            lv_arc_set_rotation(p->arcs[s * 4 + 3], (int)(-sg * p->ang[1]));
        }
        break;
    case ROTOR_RADAR: {                                    /* the wedge always trails the arm */
        float sp = 40.0f * (p->s < 0 ? -p->s : p->s);
        lv_arc_set_angles(p->arcs[0], 0, (int)sp);
        lv_arc_set_rotation(p->arcs[0], p->s > 0 ? (int)(p->ang[0] - sp) : (int)p->ang[0]);
        sweep_line(p, 0, p->ang[0], g[2] - 2);
        break;
    }
    case ROTOR_GEAR:
        rotor_gear_pts(p);
        lv_arc_set_rotation(p->arcs[0], (int)p->ang[1]);
        lv_arc_set_rotation(p->arcs[1], (int)p->ang[2]);
        break;
    }
}

static void make_rotor(console_t *c, part_t *p)
{
    const skin_desc_t *k = c->sk;
    int x = p->d->g[0], y = p->d->g[1], r = p->d->g[2];
    lv_color_t fr = CP(k->frame), tr = CP(k->trace), ac = CP(k->accent);
    lv_color_t light = lv_color_mix(lv_color_white(), fr, 26), dark = lv_color_mix(lv_color_black(), fr, 84);
    static const float BASE[6][3] = {{2, 4, 9}, {7, 2, 1}, {1, 2, 3}, {3, 6, 1}, {4, 1, 1}, {1, 3, 6}};
    int kind = p->d->style % 6;
    if (kind == ROTOR_RADAR || kind == ROTOR_RETICLE) {   /* blips, under the sweep */
        static const int8_t B[2][3][2] = {{{18, -14}, {-12, 22}, {-26, -8}}, {{14, -18}, {-22, 10}, {0, 0}}};
        int set = kind == ROTOR_RADAR ? 0 : 1;
        p->nblip = set ? 2 : 3;
        for (int i = 0; i < p->nblip; i++) {
            p->bx[i] = B[set][i][0] * r / 46.0f;
            p->by[i] = B[set][i][1] * r / 46.0f;
            p->bh[i] = (rnd() % 628) / 100.0f;
            p->blip[i] = box(c->scr, x + (int)p->bx[i] - 3, y + (int)p->by[i] - 3, 6, 6, ac);
            lv_obj_set_style_radius(p->blip[i], LV_RADIUS_CIRCLE, 0);
        }
    }
    switch (kind) {
    case ROTOR_SCOMP:
        for (int i = 0; i < 3; i++) p->arcs[p->narcs++] = ring(c->scr, x, y, r - 4, 8, 80, light);
        p->arcs[p->narcs++] = ring(c->scr, x, y, r - 18, 7, 250, dark);
        p->arcs[p->narcs++] = ring(c->scr, x, y, r - 30, 4, 60, tr);
        p->arcs[p->narcs++] = ring(c->scr, x, y, r - 30, 4, 60, tr);
        break;
    case ROTOR_RETICLE:
        for (int g = 0; g < 3; g++)                        /* fading trail behind the sweep */
            add_obj(p, pline(c->scr, &p->pts[p->no * 34], 2, lv_color_mix(tr, lv_color_black(), 40 + g * 40), 1));
        add_obj(p, pline(c->scr, &p->pts[p->no * 34], 2, tr, 2));
        p->arcs[p->narcs++] = ring(c->scr, x, y, r + 7, 3, 50, ac);
        break;
    case ROTOR_SQUARE: {
        lv_color_t cols[3] = {fr, tr, ac};
        for (int s = 0; s < 3; s++)
            for (int i = 0; i < 4; i++) add_obj(p, pline(c->scr, &p->pts[p->no * 34], 3, cols[s], 4 - s));
        break;
    }
    case ROTOR_TWIN:
        for (int s = 0; s < 2; s++) {
            int xx = x + (s ? 24 : -24);
            for (int i = 0; i < 3; i++) p->arcs[p->narcs++] = ring(c->scr, xx, y, 18, 6, 70, light);
            p->arcs[p->narcs++] = ring(c->scr, xx, y, 9, 3, 90, tr);
        }
        break;
    case ROTOR_RADAR: {
        lv_obj_t *w = ring(c->scr, x, y, r - 2, r - 2, 40, tr);    /* the sweep wedge */
        lv_obj_set_style_arc_opa(w, 70, LV_PART_INDICATOR);
        p->arcs[p->narcs++] = w;
        add_obj(p, pline(c->scr, &p->pts[p->no * 34], 2, tr, 2));
        break;
    }
    case ROTOR_GEAR:
        add_obj(p, pline(c->scr, p->pts, 33, fr, 2));
        p->arcs[p->narcs++] = ring(c->scr, x, y, r - 14, 6, 200, tr);
        p->arcs[p->narcs++] = ring(c->scr, x, y, r - 26, 4, 90, ac);
        break;
    }
    for (int i = 0; i < 3; i++) {
        p->base[i] = BASE[kind][i];
        p->spd[i] = (i & 1) ? -p->base[i] : p->base[i];
        p->ang[i] = i * 90;
        p->rev_next[i] = 160 + rnd() % 161;                /* 8-16 s */
    }
    p->s = 1;
    rotor_draw(p);
}

/* blips wander slowly (about 1 px/s) and stay inside the circle */
static void blips_drift(part_t *p)
{
    const int16_t *g = p->d->g;
    float lim = g[2] * 0.75f;
    for (int i = 0; i < p->nblip; i++) {
        p->bh[i] += ((int)(rnd() % 21) - 10) * 0.02f;     /* heading wanders */
        float nx = p->bx[i] + 0.05f * cosf(p->bh[i]), ny = p->by[i] + 0.05f * sinf(p->bh[i]);
        if (nx * nx + ny * ny > lim * lim) {               /* at the edge: turn back inwards */
            p->bh[i] = (float)atan2(-p->by[i], -p->bx[i]) + ((int)(rnd() % 101) - 50) * 0.01f;
            continue;
        }
        p->bx[i] = nx; p->by[i] = ny;
        int px = g[0] + (int)floorf(nx + 0.5f) - 3, py = g[1] + (int)floorf(ny + 0.5f) - 3;
        if (px != lv_obj_get_x(p->blip[i]) || py != lv_obj_get_y(p->blip[i])) lv_obj_set_pos(p->blip[i], px, py);
    }
}

static void console_build(console_t *c, lv_color_t trace)
{
    const skin_desc_t *k = c->sk;
    c->trace = trace;
    c->shown_temp = -999999;
    make_small_lamps(c);
    c->np = k->nparts > MAX_PARTS ? MAX_PARTS : k->nparts;
    for (int i = 0; i < c->np; i++) {
        part_t *p = &c->part[i];
        p->d = &k->parts[i];
        switch (p->d->kind) {
        case PK_SCOPE: case PK_ROUND: make_scope(c, p); break;
        case PK_VSCOPE: make_vscope(c, p); break;
        case PK_TARGET: make_target(c, p); break;
        case PK_WATERFALL: make_waterfall(c, p); break;
        case PK_METER: p->shown = -1; make_meter(c, p); break;
        case PK_BATT: make_batt(c, p); break;
        case PK_LAMPS: make_lamps(c, p); break;
        case PK_EDGE: make_edge(c, p); break;
        case PK_READOUT: make_ticker(c, p); break;
        case PK_ROTOR: make_rotor(c, p); break;
        case PK_BARS: make_bars(c, p); break;
        case PK_RING: make_ringmeter(c, p); break;
        case PK_GAUGE: make_gauge(c, p); if (p->d->flags & PF_TEMP) make_temp_label(c, p); break;
        case PK_HEX: make_hex(c, p); if (p->d->flags & PF_TEMP) c->temp_part = p; break;
        case PK_TEMP: make_temp_label(c, p); break;
        }
    }
    temp_update(c);          /* place the secret temp now, not half a second later */
}

/* free what parts allocated outside LVGL objects (bar graph pixel buffers) */
static void console_free(console_t *c)
{
    for (int i = 0; i < c->np; i++)
        if (c->part[i].cbuf) { lv_mem_free(c->part[i].cbuf); c->part[i].cbuf = NULL; }
}

/* per-frame voice updates of every part (40 ms) */
static void console_voice(console_t *c, int lvl, bool keyed, bool muted)
{
    lv_color_t fc = muted ? COL_AMBER : COL_RED;
    for (int i = 0; i < c->np; i++) {
        part_t *p = &c->part[i];
        switch (p->d->kind) {
        case PK_SCOPE: case PK_ROUND: lv_chart_set_next_value(p->chart, p->ser, lvl < 2 ? 2 : lvl); break;
        case PK_TARGET:
            lv_chart_set_next_value(p->chart, p->ser, lvl < 2 ? 2 : lvl);
            p->s += ((keyed ? 100 : 0) - p->s) * 0.2f;    /* brackets lock on while the gate is open */
            target_brackets(p);
            break;
        case PK_VSCOPE: vscope_update(p); break;
        case PK_WATERFALL: if (fast_ticks % 2 == 0) waterfall_update(p); break;
        case PK_METER: meter_update(c, p, lvl); break;
        case PK_BARS: if (p->canvas && fast_ticks % 3 == 0) bars_update(c, p); break;
        case PK_RING: ringmeter_update(p, lvl); break;
        case PK_GAUGE: gauge_update(p, lvl); break;
        }
        /* keyed: overlay the frame in amber (muted) or red (transmitting) */
        for (int f = 0; f < p->nfr; f++) {
            if (!keyed) { lv_obj_add_flag(p->fr[f], LV_OBJ_FLAG_HIDDEN); continue; }
            lv_obj_clear_flag(p->fr[f], LV_OBJ_FLAG_HIDDEN);
            if (p->d->kind == PK_ROUND) lv_obj_set_style_arc_color(p->fr[f], fc, LV_PART_INDICATOR);
            else lv_obj_set_style_line_color(p->fr[f], fc, 0);
        }
    }
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
    c->callsign = label(c->scr, S.callsign, &lv_font_montserrat_22, CP(c->sk->accent));
    callsign_show(c);
    console_build(c, CP(c->sk->trace));

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

    cap_style_mode = c->sk->key_style;                    /* the skin's key style for SYS / LIVE */
    cap_sys = make_cap(c->scr, 132, 320, 104, 114, "SYS", "SETTINGS", CP(c->sk->key), false, sys_cb, LV_EVENT_CLICKED, NULL);
    cap_live = make_cap(c->scr, 248, 320, 104, 114, "MUTED", "TAP FOR LIVE", CP(c->sk->accent), false, live_cb, LV_EVENT_CLICKED, NULL);
    cap_style_mode = KEYS_BEVEL;
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
    c->callsign = label(c->scr, S.callsign, &lv_font_montserrat_22, CP(c->sk->accent));
    callsign_show(c);
    console_build(c, CP(c->sk->accent));
    lock_bar = box(c->scr, 52, 386, 32, 10, CP(c->sk->accent));
    for (int i = 0; i < 2; i++) {
        lock_hex[i] = label(c->scr, "0000", &lv_font_montserrat_20, CP(c->sk->hexc));
        center_at(lock_hex[i], 184, 378 + i * 24);
    }
    for (int i = 0; i < 5; i++) lock_sig[i] = box(c->scr, 264 + i * 15, 394, 10, 20, COL_GREEN);
    /* every child passes taps through to the screen */
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(c->scr); i++)
        lv_obj_clear_flag(lv_obj_get_child(c->scr, i), LV_OBJ_FLAG_CLICKABLE);
}

/* ================= SETTINGS ================= */
enum { P_VOLUME, P_EFFECT, P_VOX, P_STATIC, P_CLICK, P_BRIGHT, P_CALLSIGN, P_RESET, P_DISPLAY, P_SENSORS, P_SKIN, NPAGES };
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
    {"SKIN", "", 0, 0},
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
static void carousel_refresh(void);
static lv_obj_t *grp_skin;
static cap_t cap_skin[3];             /* < SKIN | APPLY | SKIN > */
static int skin_sel, cur_skin = -1;

static void page_show(void)
{
    lv_label_set_text(set_title, PAGE[page].title);
    lv_label_set_text_fmt(set_count, "%d/%d", page + 1, NPAGES);
    lv_obj_update_layout(set_count);
    lv_obj_set_pos(set_count, 322 - lv_obj_get_width(set_count), 19);
    lv_obj_set_x(set_pip, 46 + page * 25);
    bool three = (page == P_SENSORS), skinp = (page == P_SKIN);
    bool wide = (page == P_CALLSIGN || page == P_RESET || page == P_DISPLAY);
    lv_img_set_src(set_bg, skinp ? &skin_set_skin : three ? &skin_set_sensors : wide ? &skin_set_wide : &skin_set_value);
    if (skinp) lv_obj_clear_flag(grp_skin, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(grp_skin, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 3; i++) {
        if (skinp) lv_obj_clear_flag(cap_skin[i].cap, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(cap_skin[i].cap, LV_OBJ_FLAG_HIDDEN);
    }
    if (wide || three || skinp) { lv_obj_add_flag(grp_value, LV_OBJ_FLAG_HIDDEN); }
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
    if (!wide && !three && !skinp) { lv_obj_clear_flag(cap_minus.cap, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(cap_plus.cap, LV_OBJ_FLAG_HIDDEN); }
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
    } else if (page == P_SKIN) {
        skin_sel = cur_skin;
        carousel_refresh();
    } else if (page == P_SENSORS) {
        sensors_invalidate();                              /* fresh readings when opened */
        toggles_refresh();
        sensors_readout();
    } else {
        lv_label_set_text(g_desc, PAGE[page].desc);
        value_refresh();
    }
    lv_obj_align(cap_wide.lbl, LV_ALIGN_CENTER, 0, -10);
    lv_obj_align(cap_wide.sub, LV_ALIGN_CENTER, 0, 13);
}

/* ================= skins =================
 * A skin = background art for MAIN + LOCK (RLE RGB565, decoded into RAM) + a recipe for
 * the live widgets (skin.h). Applying one rebuilds both console screens. Settings and
 * callsign screens keep the standard look. */
static uint8_t *skin_buf[2];
static lv_img_dsc_t thumb_dsc[64];

static void rle_decode(const uint8_t *src, uint32_t len, uint8_t *dst, uint32_t cap)
{
    uint32_t i = 0, o = 0;
    while (i < len && o + 2 <= cap) {
        uint8_t n = src[i++];
        if (n & 0x80) {                                     /* run: one pixel repeated n-126 times */
            int cnt = n - 126;
            uint8_t lo = src[i], hi = src[i + 1];
            i += 2;
            while (cnt-- && o + 2 <= cap) { dst[o++] = lo; dst[o++] = hi; }
        } else {                                            /* n+1 literal pixels */
            uint32_t b = (uint32_t)(n + 1) * 2;
            if (o + b > cap) b = cap - o;
            memcpy(dst + o, src + i, b);
            i += (uint32_t)(n + 1) * 2;
            o += b;
        }
    }
}

static void build_main(void);
static void build_lock(void);

static void skin_apply(int idx)
{
    if (idx < 0 || idx >= SKIN_COUNT) idx = 0;
    const skin_desc_t *sk = &SKIN_TABLE[idx];
    const uint32_t BYTES = SCR_W * SCR_H * 2;
    if (lock_cover_timer) { lv_timer_del(lock_cover_timer); lock_cover_timer = NULL; }
    if (con_main.scr) lv_obj_del(con_main.scr);           /* old screens go before their art is reused */
    if (con_lock.scr) lv_obj_del(con_lock.scr);
    if (sk->bg_main) {
        for (int i = 0; i < 2; i++)
            if (!skin_buf[i]) skin_buf[i] = lv_mem_alloc(BYTES);   /* PSRAM on the device */
        if (!skin_buf[0] || !skin_buf[1]) { sk = &SKIN_TABLE[0]; idx = 0; }   /* no memory: default skin */
    }
    if (sk->bg_main) {
        rle_decode(sk->bg_main, sk->bg_main_len, skin_buf[0], BYTES);
        rle_decode(sk->bg_lock, sk->bg_lock_len, skin_buf[1], BYTES);
        skin_main = mk_skin(skin_buf[0]);
        skin_lock = mk_skin(skin_buf[1]);
    } else {
        skin_main = mk_skin(skin_main_px);
        skin_lock = mk_skin(skin_lock_px);
    }
    lv_img_cache_invalidate_src(NULL);
    console_free(&con_main);
    console_free(&con_lock);
    memset(&con_main, 0, sizeof(con_main));
    memset(&con_lock, 0, sizeof(con_lock));
    con_main.sk = con_lock.sk = sk;
    build_main();
    build_lock();
    cur_skin = idx;
}

/* ---- SKIN page: thumbnail carousel (prev | current | next) ---- */
static lv_obj_t *car_img[3], *car_name, *car_active;

static void carousel_refresh(void)
{
    for (int k = 0; k < 3; k++) {
        int i = (skin_sel + k - 1 + SKIN_COUNT) % SKIN_COUNT;
        lv_img_set_src(car_img[k], &thumb_dsc[i]);
    }
    lv_label_set_text_fmt(car_name, "%d  %s", skin_sel + 1, SKIN_TABLE[skin_sel].name);
    center_at(car_name, 184, 188);
    bool active = (skin_sel == cur_skin);
    lv_label_set_text(cap_skin[1].lbl, active ? "ACTIVE" : "APPLY");
    lv_label_set_text(cap_skin[1].sub, active ? "IN USE" : "USE SKIN");
    cap_style(&cap_skin[1], !active);
    lv_obj_align(cap_skin[1].lbl, LV_ALIGN_CENTER, 0, -10);
    lv_obj_align(cap_skin[1].sub, LV_ALIGN_CENTER, 0, 13);
    if (active) lv_obj_clear_flag(car_active, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(car_active, LV_OBJ_FLAG_HIDDEN);
}

static void skin_step_cb(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    skin_sel = (skin_sel + dir + SKIN_COUNT) % SKIN_COUNT;
    carousel_refresh();
}

static void skin_apply_cb(lv_event_t *e)
{
    if (skin_sel == cur_skin) return;
    skin_apply(skin_sel);
    S.skin = (uint8_t)skin_sel;
    ui_platform_apply(&S);
    carousel_refresh();
}

static void build_skin_page(lv_obj_t *scr)
{
    grp_skin = group(scr);
    for (int i = 0; i < SKIN_COUNT && i < 64; i++) {
        memset(&thumb_dsc[i], 0, sizeof(lv_img_dsc_t));
        thumb_dsc[i].header.cf = LV_IMG_CF_TRUE_COLOR;
        thumb_dsc[i].header.w = THUMB_W;
        thumb_dsc[i].header.h = THUMB_H;
        thumb_dsc[i].data_size = THUMB_W * THUMB_H * 2;
        thumb_dsc[i].data = SKIN_TABLE[i].thumb;
    }
    static const int X[3] = {62, 184, 306};
    for (int k = 0; k < 3; k++) {
        lv_obj_t *im = lv_img_create(grp_skin);
        lv_obj_set_size(im, THUMB_W, THUMB_H);
        lv_obj_set_pos(im, X[k] - THUMB_W / 2, 64 + (k == 1 ? 0 : 6));
        if (k != 1) {                                       /* neighbours: smaller and dimmed */
            lv_img_set_zoom(im, 170);
            lv_obj_set_style_img_opa(im, LV_OPA_50, 0);
            lv_obj_add_flag(im, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(im, skin_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(k - 1));
        }
        car_img[k] = im;
    }
    car_name = label(grp_skin, "", &lv_font_montserrat_14, COL_TEXT);
    car_active = box(grp_skin, 184 - 20, 182 + 17, 40, 3, COL_GREEN);
    cap_skin[0] = make_cap(scr, 16, 204, 104, 104, LV_SYMBOL_LEFT, "SKIN", COL_BLUE, true,
                           skin_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    cap_skin[1] = make_cap(scr, 132, 204, 104, 104, "APPLY", "USE SKIN", COL_GREEN, false,
                           skin_apply_cb, LV_EVENT_CLICKED, NULL);
    cap_skin[2] = make_cap(scr, 248, 204, 104, 104, LV_SYMBOL_RIGHT, "SKIN", COL_BLUE, true,
                           skin_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
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
    sensors_invalidate();
    toggles_refresh();
    ui_platform_apply(&S);
}

static void sensors_readout(void)
{
    if (page != P_SENSORS) return;
    int pct = batt_get();
    if (pct < 0) lv_label_set_text(sens_batt, "PWR  USB");
    else lv_label_set_text_fmt(sens_batt, "PWR  %d%%", pct);
    float x = 0, y = 0;
    if (ui_platform_tilt(&x, &y))
        lv_label_set_text_fmt(sens_tilt, "TILT %+d %+d", (int)x, (int)y);
    else
        lv_label_set_text(sens_tilt, "TILT  --");
    float tc;
    if (temp_get(&tc)) {
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
    set_pip = box(scr_settings, 46, 46, 20, 4, COL_AMBER);

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
    build_skin_page(scr_settings);
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
    hist_push(lvl);
    fast_ticks++;
    console_voice(c, lvl, keyed, muted);
    lv_obj_set_style_bg_color(c->tx, keyed && !muted ? CP(c->sk->accent) : COL_PANEL, 0);
    lv_obj_set_style_bg_color(c->clip, audio_get_clip() ? COL_RED : COL_PANEL, 0);
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
    for (int i = 0; c && i < c->np; i++) {
        part_t *p = &c->part[i];
        if (p->d->kind != PK_ROTOR) continue;
        for (int r = 0; r < 3; r++) {
            if (--p->rev_next[r] == 0) {                               /* reverse every 8-16 s, */
                float m = (float)(1 + rnd() % 3) * p->base[r] / 2.0f;  /* at a new random speed */
                p->spd[r] = p->spd[r] > 0 ? -m : m;
                p->rev_next[r] = 160 + rnd() % 161;
            }
            p->ang[r] += p->spd[r] * boost;
            while (p->ang[r] >= 360) p->ang[r] -= 360;
            while (p->ang[r] < 0) p->ang[r] += 360;
        }
        p->s += ((p->spd[0] >= 0 ? 1.0f : -1.0f) - p->s) * 0.25f;   /* trail swings to the new side */
        blips_drift(p);
        rotor_draw(p);
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
            for (int k = 0; k < n && c->nlamps; k++) lamp_flip(c, rnd() % c->nlamps);
            if (!muted) tick_change(c);
        }
        for (int i = 0; i < c->nlamps; i++) {
            if (busy && c->lamp_next[i] > 30) c->lamp_next[i] = lamp_delay(true);
            if (--c->lamp_next[i] == 0) { lamp_flip(c, i); c->lamp_next[i] = lamp_delay(busy); }
            if (c->lamp_st[i] == 2) {                                   /* slow red pulse, ~6 s */
                int ph = (tick + i * 8) % 60, tri = ph < 30 ? ph : 60 - ph;
                lamp_paint(c, i, lamp_color(c, i), 120 + tri * 4);
            } else {
                lamp_paint(c, i, lamp_color(c, i), LV_OPA_COVER);
            }
        }
        for (int i = 0; i < c->np; i++)                                 /* hex readings drift */
            if (c->part[i].d->kind == PK_HEX && rnd() % (busy ? 3 : 12) == 0) hex_tick(c, &c->part[i]);
        if (--c->tick_next <= 0) { tick_change(c); c->tick_next = 15 + rnd() % 16; }  /* 1.5-3 s */
        if (tick % 5 == 0) temp_update(c);
    }
    was_keyed = keyed;

    if (lv_scr_act() == con_lock.scr) {
        if (tick % 4 == 0) {
            lv_label_set_text_fmt(lock_hex[0], "%04X", (unsigned)(rnd() & 0xFFFF));
            lv_label_set_text_fmt(lock_hex[1], "%04X", (unsigned)(rnd() & 0xFFFF));
            lv_obj_set_style_text_color(lock_hex[0], CP(con_lock.sk->hexc), 0);
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

    skin_set_value = mk_skin(skin_settings_value_px);
    skin_set_wide = mk_skin(skin_settings_wide_px);
    skin_callsign = mk_skin(skin_callsign_px);
    skin_set_sensors = mk_skin(skin_settings_sensors_px);
    skin_set_skin = mk_skin(skin_settings_skin_px);

    f_aureb13 = lv_tiny_ttf_create_data(aurebesh_ttf_start, aurebesh_ttf_len, 14);
    f_aureb12 = lv_tiny_ttf_create_data(aurebesh_ttf_start, aurebesh_ttf_len, 13);
    f_aureng26 = lv_tiny_ttf_create_data(aureng_ttf_start, aureng_ttf_len, 26);
    f_aureng20 = lv_tiny_ttf_create_data(aureng_ttf_start, aureng_ttf_len, 20);
    f_aureng28 = lv_tiny_ttf_create_data(aureng_ttf_start, aureng_ttf_len, 40);   /* settings page */
    f_aureng42 = lv_tiny_ttf_create_data(aureng_ttf_start, aureng_ttf_len, 42);
    f_aureng34 = lv_tiny_ttf_create_data(aureng_ttf_start, aureng_ttf_len, 34);
    f_aureng22 = lv_tiny_ttf_create_data(aureng_ttf_start, aureng_ttf_len, 22);
    f_aureng18 = lv_tiny_ttf_create_data(aureng_ttf_start, aureng_ttf_len, 18);
    f_aureng16 = lv_tiny_ttf_create_data(aureng_ttf_start, aureng_ttf_len, 16);
    f_aureng14 = lv_tiny_ttf_create_data(aureng_ttf_start, aureng_ttf_len, 14);

    build_settings();                       /* before skin_apply: the SKIN page owns the carousel */
    skin_apply(S.skin);
    build_callsign();
    build_blank();

    lv_timer_create(fast_cb, 40, NULL);
    lv_timer_create(ring_cb, 50, NULL);
    lv_timer_create(lamp_cb, 100, NULL);
    lv_scr_load(con_main.scr);
}

#ifdef UI_SIM
/* simulator-only probe for tests: current speed (deg/tick, sign = direction) of a main-screen ring */
__attribute__((export_name("ui_dbg_ring_spd"))) float ui_dbg_ring_spd(int r)
{
    for (int i = 0; i < con_main.np; i++)
        if (con_main.part[i].d->kind == PK_ROTOR) return con_main.part[i].spd[r % 3];
    return 0;
}
#endif
