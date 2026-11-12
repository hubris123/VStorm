"""Touch layout guide for the 368x448 AMOLED (see docs/UI-DESIGN.md section 4).
Draws the visible rounded screen, corner dead zones, edge margin, and 3-column key
cells at the 9 mm (114 px) primary-key size. Run: python3 design/make_touch_grid.py"""
from PIL import Image, ImageDraw

W, H, R = 368, 448, 45
PX_MM = 12.7                     # 322 ppi
MARGIN, GAP = 16, 12
COLS = 3
CELL_W = (W - 2 * MARGIN - (COLS - 1) * GAP) // COLS    # 104
KEY_H = round(9 * PX_MM)                                # 114

S = 2                            # draw at 2x for legibility
img = Image.new("RGB", (W * S, H * S), (0, 0, 0))
d = ImageDraw.Draw(img)
r = lambda *v: [x * S for x in v]

d.rounded_rectangle(r(0, 0, W - 1, H - 1), radius=R * S, outline=(200, 200, 200), width=2)
for cx, cy in [(0, 0), (W - R, 0), (0, H - R), (W - R, H - R)]:
    d.rectangle(r(cx, cy, cx + R, cy + R), outline=(150, 30, 36), width=1)
d.rectangle(r(14, 14, W - 14, H - 14), outline=(60, 90, 110), width=1)

# two rows of primary keys in the bottom half (thumb reach), one row of secondary above
y = H - MARGIN - 14 - KEY_H
for row in range(2):
    for c in range(COLS):
        x = MARGIN + c * (CELL_W + GAP)
        d.rectangle(r(x, y, x + CELL_W, y + KEY_H), outline=(69, 159, 194), width=2)
        d.ellipse(r(x + CELL_W / 2 - 57, y + KEY_H / 2 - 57, x + CELL_W / 2 + 57, y + KEY_H / 2 + 57),
                  outline=(224, 165, 38), width=1)
    y -= KEY_H + GAP

d.text(r(MARGIN + 4, 20), f"368x448  12.7 px/mm  key {CELL_W}x{KEY_H}px = 8.2x9 mm", fill=(201, 205, 208))
d.text(r(MARGIN + 4, 34), "blue: key cell  amber: 9 mm finger  red: corner dead zone", fill=(130, 136, 140))
for mm in range(0, 30, 5):          # mm ruler along the top
    x = MARGIN + mm * PX_MM
    d.line(r(x, 52, x, 58), fill=(130, 136, 140))
    d.text(r(x + 1, 58), f"{mm}", fill=(130, 136, 140))
img.save("design/touch_grid.png")
print("wrote design/touch_grid.png", img.size, "cell", CELL_W, "x", KEY_H)
