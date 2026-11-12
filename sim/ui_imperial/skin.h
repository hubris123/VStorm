#pragma once
/* Skin recipe: a background layer (static art) + which live widgets go where, in what
 * colours. Generated from design/skins/packs/skins.json (design/skins/concepts/make_concepts.py)
 * by sim/build.sh into skins_gen.c. On the product these come from signed .vskin files on
 * the SD card (docs/PRODUCT-NOTES.md); skin 0 is built in. */
#include <stdint.h>

enum { FRAME_NOTCHED, FRAME_BRACKETS, FRAME_DOUBLE, FRAME_TICKS, FRAME_DASHED };
enum { METER_VSEG, METER_DOTS, METER_TWIN, METER_HSEG, METER_ARC };
enum { LAMPS_GRID, LAMPS_SWITCHER, LAMPS_DOMES, LAMPS_PILLS, LAMPS_DIAMONDS, LAMPS_MATRIX };
enum { ROTOR_SCOMP, ROTOR_RETICLE, ROTOR_SQUARE, ROTOR_TWIN, ROTOR_RADAR, ROTOR_GEAR };
enum { KEYS_BEVEL, KEYS_FLAT, KEYS_STRIP, KEYS_RING, KEYS_PILL };

#define THUMB_W 96
#define THUMB_H 117

typedef struct {
    const char *name;
    /* palette */
    uint32_t frame, grid, trace, accent, text, tick, key, hexc, lamp[4];
    /* widget kinds */
    uint8_t scope_round, frame_kind, meter_kind, batt_h, lamps_kind, rotor_kind, key_style;
    /* geometry (screen px) */
    int16_t sc[4];   /* scope: rect x0,y0,x1,y1  |  round cx,cy,r                         */
    int16_t mt[5];   /* meter: vseg x,y0,y1,w,n | dots x,y0,y1,n | twin x,y0,y1 |
                        hseg x0,x1,y,h,n | arc cx,cy,r                                      */
    int16_t bt[4];   /* battery: vertical x,y0,y1,w  |  horizontal x0,x1,y,h                */
    int16_t lp[4];   /* lamps: cx,cy (+ cols,rows for the switcher bank)                    */
    int16_t ro[4];   /* readout: cx,cy,lines,pitch                                          */
    int16_t rt[3];   /* rotor: cx,cy,r                                                      */
    /* background layers, RLE-compressed RGB565 (NULL = the built-in default art) */
    const uint8_t *bg_main, *bg_lock;
    uint32_t bg_main_len, bg_lock_len;
    const uint8_t *thumb;  /* THUMB_W x THUMB_H RGB565 for the SKIN page carousel */
} skin_desc_t;

extern const skin_desc_t SKIN_TABLE[];
extern const int SKIN_COUNT;
