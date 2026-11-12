#pragma once
/* Skin recipe: a background layer (static art) + a list of live parts (what goes where, in
 * what colours). Generated from design/skins/packs/skins.json (design/skins/concepts/
 * make_concepts.py + make_concepts2.py) by sim/build.sh into skins_gen.c. On the product these
 * come from signed .vskin files on the SD card (docs/PRODUCT-NOTES.md); skin 0 is built in.
 *
 * The top bar (callsign, TX, CLIP) and the LOCK / SYS / LIVE keys are on every skin; the
 * parts fill the console area between them. */
#include <stdint.h>

enum { FRAME_NOTCHED, FRAME_BRACKETS, FRAME_DOUBLE, FRAME_TICKS, FRAME_DASHED };
enum { METER_VSEG, METER_DOTS, METER_TWIN, METER_HSEG, METER_ARC };
enum { LAMPS_GRID, LAMPS_SWITCHER, LAMPS_DOMES, LAMPS_PILLS, LAMPS_DIAMONDS, LAMPS_MATRIX };
enum { ROTOR_SCOMP, ROTOR_RETICLE, ROTOR_SQUARE, ROTOR_TWIN, ROTOR_RADAR, ROTOR_GEAR };
enum { KEYS_BEVEL, KEYS_FLAT, KEYS_STRIP, KEYS_RING, KEYS_PILL };

/* part kinds and their geometry g[] (screen px) */
enum {
    PK_SCOPE,      /* style frame   g x0,y0,x1,y1             voice trace, tilt lines, keyed frame   */
    PK_ROUND,      /*               g cx,cy,r                 round scope                            */
    PK_VSCOPE,     /* style frame   g x0,y0,x1,y1             tall scope: trace runs top to bottom   */
    PK_TARGET,     /*               g x0,y0,x1,y1             trace + brackets that close in on voice */
    PK_WATERFALL,  /*               g x0,y0,x1,y1,rows        stacked past traces, newest at bottom   */
    PK_METER,      /* style meter   g as METER_* (see make_meter)                                    */
    PK_BATT,       /* style 0 v/1 h g v: x,y0,y1,w  h: x0,x1,y,h                                     */
    PK_LAMPS,      /* style lamps   g cx,cy,cols,rows,r                                              */
    PK_EDGE,       /*               g x,y0,y1,w               strip of small lamps                   */
    PK_READOUT,    /*               g cx,cy,lines,pitch       Aurebesh lines (TEMP flag: temp on 0)  */
    PK_ROTOR,      /* style rotor   g cx,cy,r                                                        */
    PK_BARS,       /*               g x0,y0,x1,y1,n           segmented voice bar graph (history)    */
    PK_RING,       /*               g cx,cy,r,n,w             segmented ring meter                   */
    PK_GAUGE,      /*               g cx,cy,r                 big semicircle gauge (TEMP: on plate)  */
    PK_HEX,        /* style = temp cell  g x0,y0,cols,rows,cw,rh   hex readings (TEMP: one cell)     */
    PK_TEMP,       /* style prefix  g x,y,anchor(0 mid,1 right,2 left),font px                       */
};
#define PF_TEMP 1          /* this part shows the secret temperature */
#define SKIN_MAX_PARTS 12

typedef struct {
    uint8_t kind, style, flags;
    int16_t g[6];
} skin_part_t;

#define THUMB_W 96
#define THUMB_H 117

typedef struct {
    const char *name;
    /* palette */
    uint32_t frame, grid, trace, accent, text, tick, key, hexc, lamp[4];
    uint8_t key_style, nparts;
    skin_part_t parts[SKIN_MAX_PARTS];
    /* background layers, RLE-compressed RGB565 (NULL = the built-in default art) */
    const uint8_t *bg_main, *bg_lock;
    uint32_t bg_main_len, bg_lock_len;
    const uint8_t *thumb;  /* THUMB_W x THUMB_H RGB565 for the SKIN page carousel */
} skin_desc_t;

extern const skin_desc_t SKIN_TABLE[];
extern const int SKIN_COUNT;
