"""Default skin: the static background layer of every Imperial UI screen.

Each PNG is the panel art (key housings, cap wells, frames, grids, fixed labels) at the
panel's native 368x448. The live LVGL widgets in sim/ui_imperial/ui_imperial.c sit on top
at matching coordinates. A future skin = a new set of these PNGs, same geometry.

    python3 design/skins/make_default_skin.py      (needs Pillow)
"""
import math
import os
import sys

from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "mockups"))
import make_mockups as m  # noqa: E402

OUT = os.path.join(HERE, "default")
S = m.S


def raw(sc):
    """Composite glow, downsample the 2x drawing to native 368x448 RGB."""
    glow = sc.glow.filter(ImageFilter.GaussianBlur(9 * S))
    img = Image.alpha_composite(sc.img, glow).convert("RGB")
    return img.resize((m.W, m.H), Image.LANCZOS)


def wells(sc, x, y, w, h):
    m.housing(sc, x, y, w, h)
    sc.d.rounded_rectangle(sc.b(x + 11, y + 11, x + w - 11, y + h - 11), radius=m.CAP_R * S, fill="#07090B")


def scomp_socket(sc, cx=290, cy=244):
    sc.d.ellipse(sc.b(cx - 52, cy - 52, cx + 52, cy + 52), outline="#22272B", width=2 * S)
    for k in range(24):
        a = math.radians(k * 15)
        sc.line([(cx + 47 * math.cos(a), cy + 47 * math.sin(a)),
                 (cx + 51 * math.cos(a), cy + 51 * math.sin(a))], "#2E3438", 1)
    sc.d.ellipse(sc.b(cx - 7, cy - 7, cx + 7, cy + 7), fill="#B9BEC2")
    sc.d.ellipse(sc.b(cx - 3, cy - 3, cx + 3, cy + 3), fill="#0C0F12")


def meter_wells(sc):
    for i in range(12):
        sc.rect(306, 178 - i * 11 - 8, 342, 178 - i * 11, fill="#07090B")


def main_bg():
    sc = m.Screen()
    for x, label in ((247, "TX"), (311, "CLIP")):
        sc.text(x, 29, label, m.font(m.MONT, 13), m.LABEL, "rm")
    m.scope(sc, 18, 62, 288, 176, m.BLUE, [], trace=False, frame_col=None)
    meter_wells(sc)
    scomp_socket(sc)
    m.housing(sc, 16, 304, 104, 114)                       # lock: housing + switch nut, no well
    cx, cy = 68, 367
    sc.d.ellipse(sc.b(cx - 20, cy - 20, cx + 20, cy + 20), fill="#2A3036", outline="#6A7177", width=2 * S)
    sc.d.ellipse(sc.b(cx - 11, cy - 11, cx + 11, cy + 11), fill="#151A1E", outline="#4C5358", width=S)
    sc.text(68, 405, "LOCK", m.font(m.MONT, 13), m.LABEL, "mm")
    wells(sc, 132, 304, 104, 114)
    wells(sc, 248, 304, 104, 114)
    return sc


def lock_bg():
    sc = m.Screen()
    for x, label in ((247, "TX"), (311, "CLIP")):
        sc.text(x, 29, label, m.font(m.AUREB, 12), m.LABEL, "rm")
    m.scope(sc, 18, 62, 288, 176, m.RED, [], trace=False, frame_col=None)
    meter_wells(sc)
    scomp_socket(sc)
    for x in (16, 132, 248):
        m.housing(sc, x, 304, 104, 114)
    sc.text(68, 345, "LOCKED", m.font(m.AUREB, 15), m.RED, "mm")
    sc.text(184, 330, "ENC", m.font(m.AUREB, 12), m.FRAME, "mm")
    sc.text(300, 326, "SIG", m.font(m.AUREB, 12), m.FRAME, "mm")
    return sc


def settings_common(sc):
    for i in range(9):                                      # page pip wells (9 settings pages)
        x = 46 + i * 31
        sc.rect(x, 46, x + 25, 50, fill=m.DIM)
    m.notched_frame(sc, 16, 60, 352, 184)
    for x in (16, 132, 248):
        wells(sc, x, 312, 104, 104)


def settings_value_bg():
    sc = m.Screen()
    settings_common(sc)
    cx, cy, r = 98, 122, 50
    sc.d.ellipse(sc.b(cx - r - 10, cy - r - 10, cx + r + 10, cy + r + 10), outline="#22272B", width=2 * S)
    sc.arc(cx, cy, r, 135, 405, m.DIM, 9)
    sc.text(cx, cy + 27, "LEVEL", m.font(m.MONT, 13), m.LABEL, "mm")
    for i in range(10):
        sc.rect(176 + i * 17, 94, 189 + i * 17, 112, fill=m.DIM)
    wells(sc, 16, 196, 160, 104)
    wells(sc, 192, 196, 160, 104)
    return sc


def settings_wide_bg():
    sc = m.Screen()
    settings_common(sc)
    wells(sc, 16, 196, 336, 104)
    return sc


def callsign_bg():
    sc = m.Screen()
    sc.text(46, 28, "CALLSIGN", m.font(m.MONT, 22), m.TEXT, "lm")
    sc.text(322, 28, "CODE CYLINDER", m.font(m.MONT, 13), m.LABEL, "rm")
    n, dw, gap = 10, 29, 3
    x0 = (m.W - (n * dw + (n - 1) * gap)) / 2
    for i in range(n):
        x = x0 + i * (dw + gap)
        sc.vgrad(x, 58, x + dw, 146, "#05070A", "#05070A")
        sc.vgrad(x, 58, x + dw, 80, "#000000", "#1A1F24")
        sc.vgrad(x, 124, x + dw, 146, "#1A1F24", "#000000")
        sc.rect(x, 86, x + dw, 118, fill="#1A1F24")
        sc.rect(x, 58, x + dw, 146, outline="#2A2F34", width=1)
    wells(sc, 16, 196, 160, 104)
    wells(sc, 192, 196, 160, 104)
    for x in (16, 132, 248):
        wells(sc, x, 312, 104, 104)
    return sc


SKINS = {"main": main_bg, "lock": lock_bg, "settings_value": settings_value_bg,
         "settings_wide": settings_wide_bg, "callsign": callsign_bg}


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, fn in SKINS.items():
        raw(fn()).save(os.path.join(OUT, name + ".png"), optimize=True)
    print("wrote", ", ".join(SKINS), "to", OUT)


if __name__ == "__main__":
    main()
