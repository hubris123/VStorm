from PIL import Image, ImageDraw, ImageFont

W, H, R = 368, 448, 48          # screen size + corner radius
SAFE = 14                       # straight-edge margin
img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

try:
    f  = ImageFont.truetype("arial.ttf", 13)
    fs = ImageFont.truetype("arial.ttf", 10)
except Exception:
    f = fs = ImageFont.load_default()

# screen field (the actual visible rounded-rect)
d.rounded_rectangle([1, 1, W-2, H-2], radius=R, fill=(8, 10, 14, 255),
                    outline=(255, 255, 255, 255), width=2)

# corner dead zones: hatch the bounding-box corners that fall OUTSIDE the radius
import math
for cx, cy, sx, sy in [(R, R, -1, -1), (W-R, R, 1, -1), (R, H-R, -1, 1), (W-R, H-R, 1, 1)]:
    for px in range(0, R):
        for py in range(0, R):
            ax = cx + sx*px; ay = cy + sy*py
            if (px - R)**2 + (py - R)**2 > R*R:   # outside the corner arc
                if (px + py) % 6 == 0:
                    d.point([(ax, ay)], fill=(190, 30, 30, 200))

# safe-content area (keep important UI inside this dashed line)
def dashed_rrect(box, radius, color, dash=6):
    x0,y0,x1,y1 = box
    for x in range(x0, x1, dash*2):
        d.line([x, y0, min(x+dash, x1), y0], fill=color, width=1)
        d.line([x, y1, min(x+dash, x1), y1], fill=color, width=1)
    for y in range(y0, y1, dash*2):
        d.line([x0, y, x0, min(y+dash, y1)], fill=color, width=1)
        d.line([x1, y, x1, min(y+dash, y1)], fill=color, width=1)
dashed_rrect([SAFE, SAFE, W-SAFE, H-SAFE], R-SAFE, (90, 150, 200, 255))

# live-widget windows (from ui.c) - leave these as dark openings in the art
zones = [
    ("TOP BAR (callsign / gear)",  44,  8, 324,  48, (120,120,120)),
    ("SCOPE",                       9, 57, 285, 173, (90,150,200)),
    ("METER",                     332, 36, 354, 176, (80,150,90)),
    ("LIGHTS",                     28,196, 108, 290, (200,120,40)),
    ("KNOB",                      210,196, 290, 294, (140,140,140)),
    ("BOTTOM BAR (LOCK/TX/CLIP/LIVE)", 32,372, 336, 414, (160,60,60)),
]
for name,x0,y0,x1,y1,col in zones:
    d.rectangle([x0,y0,x1,y1], outline=col+(255,), width=2)
    d.text((x0+3, y0+2), name, fill=col+(255,), font=fs)

# title + dims
d.text((SAFE+4, H-30), "368x448  r=%d  safe=%d" % (R, SAFE), fill=(180,180,180,255), font=f)

img.save("design/screen_template.png")
print("wrote design/screen_template.png", img.size)
