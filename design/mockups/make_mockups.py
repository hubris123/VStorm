"""Image mock-ups of the Imperial console UI for the 368x448 AMOLED (UI plan step 2/3).
Draws each screen at 2x, masks it to the panel's rounded shape, and writes PNGs, a contact
sheet and an animated GIF of the main screen. Layout follows docs/UI-DESIGN.md and
design/touch_grid.png (keys 104 x 114 px = 8.2 x 9 mm).

    python3 design/mockups/make_mockups.py
"""
import math
import os
import random
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
W, H, R, S = 368, 448, 45, 2

# palette (docs/UI-DESIGN.md section 2) + lamp colours sampled for the 1.47 UI
BG, PANEL, FRAME, TEXT = "#000000", "#0C0F12", "#5C6166", "#C9CDD0"
RED, GREEN, BLUE, AMBER = "#C4262E", "#3C8A44", "#459FC2", "#E0A526"
DIM = "#3A3F44"
LAMP = {"grey": "#979B9A", "orange": "#BF5B16", "red": "#D00A1B", "blue": "#459FC2"}


def font(path, px):
    return ImageFont.truetype(path, int(px * S))


MONT = os.path.join(ROOT, "sim/.deps/lvgl/scripts/built_in_font/Montserrat-Medium.ttf")
if not os.path.exists(MONT):
    MONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
AUREB = os.path.join(ROOT, "firmware/main/aurebesh.ttf")
AURENG = os.path.join(ROOT, "firmware/main/aurebesh_english.ttf")


def hexrgb(c, a=255):
    c = c.lstrip("#")
    return tuple(int(c[i:i + 2], 16) for i in (0, 2, 4)) + (a,)


def mix(c1, c2, t):
    a, b = hexrgb(c1), hexrgb(c2)
    return "#%02X%02X%02X" % tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


class Screen:
    def __init__(self):
        self.img = Image.new("RGBA", (W * S, H * S), hexrgb(BG))
        self.glow = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.img)
        self.g = ImageDraw.Draw(self.glow)

    @staticmethod
    def b(*v):
        return [x * S for x in v]

    # ---- primitives ----
    def rect(self, x0, y0, x1, y1, fill=None, outline=None, width=1):
        self.d.rectangle(self.b(x0, y0, x1, y1), fill=fill, outline=outline, width=width * S)

    def line(self, pts, fill, width=1):
        self.d.line([(x * S, y * S) for x, y in pts], fill=fill, width=width * S)

    def text(self, x, y, s, f, fill, anchor="la"):
        self.d.text((x * S, y * S), s, font=f, fill=fill, anchor=anchor)

    def glow_rect(self, x0, y0, x1, y1, col, alpha=150, ring=False):
        if ring:   # halo around a lit key: outline only, so the label stays clean
            self.g.rectangle(self.b(x0 - 3, y0 - 3, x1 + 3, y1 + 3), outline=hexrgb(col, alpha), width=7 * S)
        else:
            self.g.rectangle(self.b(x0, y0, x1, y1), fill=hexrgb(col, alpha))

    def vgrad(self, x0, y0, x1, y1, top, bottom):
        for y in range(int(y0 * S), int(y1 * S)):
            t = (y - y0 * S) / max(1, (y1 - y0) * S)
            self.d.line([(x0 * S, y), (x1 * S, y)], fill=mix(top, bottom, t))

    def arc(self, cx, cy, r, a0, a1, col, width):
        self.d.arc(self.b(cx - r, cy - r, cx + r, cy + r), a0, a1, fill=col, width=width * S)

    def finish(self):
        glow = self.glow.filter(ImageFilter.GaussianBlur(9 * S))
        out = Image.alpha_composite(self.img, glow)
        # rounded panel mask on a bench-grey surround
        mask = Image.new("L", out.size, 0)
        ImageDraw.Draw(mask).rounded_rectangle([0, 0, W * S - 1, H * S - 1], radius=R * S, fill=255)
        pad = 14 * S
        frame = Image.new("RGBA", (out.width + 2 * pad, out.height + 2 * pad), hexrgb("#16181A"))
        ImageDraw.Draw(frame).rounded_rectangle([pad // 2, pad // 2, frame.width - pad // 2, frame.height - pad // 2],
                                                radius=(R + 7) * S, fill=hexrgb("#050506"))
        frame.paste(out, (pad, pad), mask)
        return frame.convert("RGB")


# ---------------- components ----------------
def housing(sc, x, y, w, h):
    sc.rect(x, y, x + w, y + h, fill="#121619", outline=FRAME, width=2)
    for sx, sy in ((x + 5, y + 5), (x + w - 5, y + 5), (x + 5, y + h - 5), (x + w - 5, y + h - 5)):
        sc.d.ellipse(sc.b(sx - 2, sy - 2, sx + 2, sy + 2), fill="#2A2F34")       # panel screws


def key(sc, x, y, w, h, label, accent=TEXT, lit=False, sub=None, big=False, pressed=False):
    """Square console push-button: housing + bevelled cap. lit = cap glows in accent."""
    housing(sc, x, y, w, h)
    i = 11
    cx0, cy0, cx1, cy1 = x + i, y + i + (2 if pressed else 0), x + w - i, y + h - i + (2 if pressed else 0)
    if lit:
        sc.glow_rect(cx0 - 2, cy0 - 2, cx1 + 2, cy1 + 2, accent, 190, ring=True)
        sc.vgrad(cx0, cy0, cx1, cy1, mix(accent, "#FFFFFF", 0.25), mix(accent, "#000000", 0.25))
        fg = "#FFF4F0"
    else:
        sc.vgrad(cx0, cy0, cx1, cy1, "#353C43" if not pressed else "#23292E", "#1A1E22")
        fg = accent
    sc.line([(cx0, cy1), (cx0, cy0), (cx1, cy0)], mix(TEXT, "#000000", 0.35 if not lit else 0.0), 1)  # highlight
    sc.line([(cx0, cy1), (cx1, cy1), (cx1, cy0)], "#050607", 2)                                      # shadow
    f = font(MONT, 30 if big else 17)
    ty = (cy0 + cy1) / 2 - (7 if sub else 0)
    sc.text((cx0 + cx1) / 2, ty, label, f, fg, "mm")
    if sub:
        sc.text((cx0 + cx1) / 2, ty + 20, sub, font(MONT, 10), fg if lit else FRAME, "mm")


def guarded_toggle(sc, x, y, w, h, cover_open=False, label="LOCK"):
    """Toggle switch under a striped flip-up safety cover."""
    housing(sc, x, y, w, h)
    cx, cy = x + w / 2, y + h / 2 + 6
    sc.d.ellipse(sc.b(cx - 20, cy - 20, cx + 20, cy + 20), fill="#2A3036", outline="#6A7177", width=2 * S)
    sc.d.ellipse(sc.b(cx - 11, cy - 11, cx + 11, cy + 11), fill="#151A1E", outline="#4C5358", width=S)
    sc.d.rounded_rectangle(sc.b(cx - 5, cy - 40, cx + 5, cy), radius=4 * S, fill="#B9BEC2", outline="#6E757B", width=S)
    sc.d.ellipse(sc.b(cx - 7, cy - 46, cx + 7, cy - 32), fill="#D8DCDF")
    sc.text(cx, y + h - 13, label, font(MONT, 11), FRAME, "mm")
    if cover_open:
        # cover flipped up: shows as a thick striped bar at the top hinge
        cov = (x + 14, y + 8, x + w - 14, y + 22)
    else:
        cov = (x + 14, y + 8, x + w - 14, y + h - 22)
    layer = Image.new("RGBA", sc.img.size, (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    x0, y0, x1, y1 = [v * S for v in cov]
    ld.rectangle([x0, y0, x1, y1], fill=hexrgb("#6E1015", 215))
    for k in range(-int(y1 - y0), int(x1 - x0), 16 * S):          # hazard stripes
        ld.line([(x0 + k, y1), (x0 + k + (y1 - y0), y0)], fill=hexrgb("#1A0405", 230), width=6 * S)
    lm = Image.new("L", sc.img.size, 0)
    ImageDraw.Draw(lm).rectangle([x0, y0, x1, y1], fill=255)
    layer.putalpha(Image.composite(layer.getchannel("A"), Image.new("L", sc.img.size, 0), lm))
    sc.img.alpha_composite(layer)
    sc.d = ImageDraw.Draw(sc.img)
    sc.rect(*cov, outline="#C4262E", width=2)
    sc.rect(cov[0] + 20, cov[1] - 3 + (0 if cover_open else 0), cov[2] - 20, cov[1] + 3, fill="#8A9095")  # hinge
    if not cover_open:
        sc.text((cov[0] + cov[2]) / 2, (cov[1] + cov[3]) / 2 + 4, "LOCK", font(MONT, 15), "#F0C8C8", "mm")


LAMP_LAYOUT = [(0, 0, 80, 14, False), (0, 22, 32, 14, False), (42, 22, 28, 28, True),
               (0, 44, 32, 14, False), (42, 44, 32, 14, False), (0, 66, 28, 28, True), (42, 66, 32, 14, False)]


def lamps(sc, x, y, states, opas=None):
    for i, (lx, ly, lw, lh, dome) in enumerate(LAMP_LAYOUT):
        col = LAMP[states[i]]
        a = (opas or [1] * 7)[i]
        c = mix("#000000", col, a)
        box = sc.b(x + lx, y + ly, x + lx + lw, y + ly + lh)
        if dome:
            sc.d.ellipse(box, fill=c)
            sc.d.ellipse(sc.b(x + lx + 6, y + ly + 5, x + lx + 12, y + ly + 10), fill=mix(c, "#FFFFFF", 0.35))
        else:
            sc.d.rectangle(box, fill=c)
        if states[i] in ("red", "blue") and a > 0.6:
            sc.g.rectangle(box, fill=hexrgb(col, int(90 * a)))


def scomp(sc, cx, cy, r, t=0.0):
    """Scomp link: socket plate + 3-segment outer ring, inner rotor, fast spinning tip."""
    sc.d.ellipse(sc.b(cx - r - 6, cy - r - 6, cx + r + 6, cy + r + 6), outline="#22272B", width=2 * S)
    for k in range(24):                                          # socket ticks
        a = math.radians(k * 15)
        sc.line([(cx + (r + 1) * math.cos(a), cy + (r + 1) * math.sin(a)),
                 (cx + (r + 5) * math.cos(a), cy + (r + 5) * math.sin(a))], "#2E3438", 1)
    o = 37 * t
    for i in range(3):
        sc.arc(cx, cy, r - 4, o + i * 120, o + i * 120 + 80, "#8A9095", 8)
    sc.arc(cx, cy, r - 18, -73 * t + 90, -73 * t + 90 + 250, "#5C6166", 7)
    sc.arc(cx, cy, r - 30, 160 * t, 160 * t + 60, BLUE, 4)
    sc.arc(cx, cy, r - 30, 160 * t + 180, 160 * t + 240, BLUE, 4)
    sc.d.ellipse(sc.b(cx - 7, cy - 7, cx + 7, cy + 7), fill="#B9BEC2")
    sc.d.ellipse(sc.b(cx - 3, cy - 3, cx + 3, cy + 3), fill="#0C0F12")


def notched_frame(sc, x0, y0, x1, y1, col="#8A8A8A", n=12):
    sc.line([(x0, y0 + n), (x0 + n, y0), (x1, y0), (x1, y1), (x0, y1), (x0, y0 + n)], col, 2)


def scope(sc, x0, y0, x1, y1, col, wave, phase=0):
    sc.rect(x0, y0, x1, y1, fill=PANEL, outline=FRAME)
    for k in range(1, 4):
        yy = y0 + (y1 - y0) * k / 4
        sc.line([(x0, yy), (x1, yy)], "#1E2429", 1)
    for k in range(1, 6):
        xx = x0 + (x1 - x0) * k / 6
        sc.line([(xx, y0), (xx, y1)], "#1E2429", 1)
    pts = []
    n = len(wave)
    for i, v in enumerate(wave):
        pts.append((x0 + 3 + (x1 - x0 - 6) * i / (n - 1), y1 - 4 - (y1 - y0 - 8) * v / 100))
    sc.line(pts, col, 2)
    notched_frame(sc, x0 - 3, y0 - 3, x1 + 3, y1 + 3)


def seg_meter(sc, x, ybot, level, segs=12, w=36, h=8, red_from=9):
    lit = level * segs // 100
    for i in range(segs):
        c = PANEL
        if i < lit:
            c = RED if i >= red_from else GREEN
        sc.rect(x, ybot - i * (h + 3) - h, x + w, ybot - i * (h + 3), fill=c)


def small_lamp(sc, x, y, label, on, col=RED, f=None):
    sc.rect(x, y, x + 14, y + 14, fill=col if on else PANEL, outline=FRAME)
    if on:
        sc.glow_rect(x, y, x + 14, y + 14, col, 160)
    sc.text(x - 5, y + 7, label, f or font(MONT, 12), FRAME, "rm")


def voice_wave(n, seed, t=0):
    rnd = random.Random(seed)
    out = []
    for i in range(n):
        k = i + t
        env = max(0, math.sin(k / 9.0)) ** 0.7 * (0.6 + 0.4 * math.sin(k / 23.0))
        out.append(max(2, min(98, 8 + 70 * env + rnd.uniform(-6, 6))))
    return out


TICK_WORDS = ["SEC 7", "TK 421", "AUTH 9", "SCAN", "CH 04", "GRID 2", "LINK", "SYNC 3", "ENC A7"]


def ticker(sc, x, y, lines, f, col, t=0):
    for i in range(lines):
        w = TICK_WORDS[(i * 3 + t) % len(TICK_WORDS)] + " " + TICK_WORDS[(i * 5 + t + 2) % len(TICK_WORDS)]
        sc.text(x, y + i * 18, w, f, col)


# ---------------- screens ----------------
def main_screen(live=False, cover_open=False, t=0, lamp_states=None, lamp_opas=None, level=62):
    sc = Screen()
    sc.text(46, 30, "TK-421", font(AURENG, 26), RED, "lm")
    small_lamp(sc, 252, 22, "TX", level > 22)
    small_lamp(sc, 316, 22, "CLIP", False)
    scope(sc, 18, 62, 288, 176, BLUE, voice_wave(60, 3, t))
    seg_meter(sc, 306, 178, level)
    lamps(sc, 24, 196, lamp_states or ["blue", "grey", "orange", "grey", "red", "blue", "orange"], lamp_opas)
    ticker(sc, 120, 200, 4, font(AUREB, 13), mix(AMBER, "#000000", 0.35), t // 3)
    scomp(sc, 290, 244, 46, t / 8)
    guarded_toggle(sc, 16, 304, 104, 114, cover_open)
    key(sc, 132, 304, 104, 114, "SYS", BLUE, sub="SETTINGS")
    if live:
        key(sc, 248, 304, 104, 114, "LIVE", RED, lit=True, sub="TRANSMIT")
    else:
        key(sc, 248, 304, 104, 114, "MUTED", RED, sub="TAP FOR LIVE")
    return sc


def lock_screen():
    sc = Screen()
    sc.text(46, 30, "TK-421", font(AURENG, 26), RED, "lm")
    small_lamp(sc, 252, 22, "TX", True, f=font(AUREB, 12))
    small_lamp(sc, 316, 22, "CLIP", False, f=font(AUREB, 12))
    scope(sc, 18, 62, 288, 176, RED, voice_wave(60, 5))
    seg_meter(sc, 306, 178, 55)
    lamps(sc, 24, 196, ["red", "grey", "blue", "orange", "grey", "red", "blue"])
    ticker(sc, 120, 200, 4, font(AUREB, 13), mix(AMBER, "#000000", 0.35), 4)
    scomp(sc, 290, 244, 46, 2.3)
    # control positions become read-only plates
    housing(sc, 16, 304, 104, 114)
    sc.text(68, 345, "LOCKED", font(AUREB, 15), RED, "mm")
    sc.rect(52, 370, 84, 380, fill=RED)
    sc.glow_rect(52, 370, 84, 380, RED)
    housing(sc, 132, 304, 104, 114)
    sc.text(184, 340, "ENC", font(AUREB, 12), FRAME, "mm")
    sc.text(184, 362, "49F2", font(MONT, 20), AMBER, "mm")
    sc.text(184, 386, "24BC", font(MONT, 20), AMBER, "mm")
    housing(sc, 248, 304, 104, 114)
    for i, hgt in enumerate([18, 30, 24, 40, 34]):
        sc.rect(264 + i * 15, 398 - hgt, 274 + i * 15, 398, fill=GREEN)
    sc.text(300, 328, "SIG", font(AUREB, 12), FRAME, "mm")
    return sc


def header(sc, title, page, pages):
    sc.text(46, 28, title, font(MONT, 22), TEXT, "lm")
    sc.text(322, 28, f"{page}/{pages}", font(MONT, 14), FRAME, "rm")
    for i in range(pages):
        x = 46 + i * 35
        sc.rect(x, 46, x + 29, 50, fill=AMBER if i + 1 == page else DIM)


def settings_screen(name="EFFECT", value=70, page=2, desc="DRIVE + BITCRUSH", unit=""):
    sc = Screen()
    header(sc, name, page, 8)
    # gauge window
    cx, cy, r = 98, 122, 50
    sc.d.ellipse(sc.b(cx - r - 10, cy - r - 10, cx + r + 10, cy + r + 10), outline="#22272B", width=2 * S)
    sc.arc(cx, cy, r, 135, 405, DIM, 9)
    sc.arc(cx, cy, r, 135, 135 + 270 * value / 100, AMBER, 9)
    sc.arc(cx, cy, r + 9, 200, 260, "#5C6166", 3)               # slow-spinning outer tick (alive)
    sc.text(cx, cy - 2, f"{value}{unit}", font(MONT, 30), AMBER, "mm")
    sc.text(cx, cy + 26, "LEVEL", font(MONT, 10), FRAME, "mm")
    sc.text(176, 76, desc, font(MONT, 13), TEXT, "lm")
    for i in range(10):                                         # 10-seg bar
        sc.rect(176 + i * 17, 94, 189 + i * 17, 112, fill=AMBER if i < value // 10 else DIM)
    ticker(sc, 176, 128, 2, font(AUREB, 12), mix(AMBER, "#000000", 0.45), 2)
    for i, c in enumerate(["blue", "red", "grey"]):   # small status lamps (drift like the main cluster)
        sc.rect(176 + i * 22, 168, 192 + i * 22, 176, fill=LAMP[c])
    notched_frame(sc, 16, 60, 352, 184)
    key(sc, 16, 196, 160, 104, "−", BLUE, sub="HOLD TO REPEAT", big=True)
    key(sc, 192, 196, 160, 104, "+", BLUE, sub="HOLD TO REPEAT", big=True)
    key(sc, 16, 312, 104, 104, "◀", BLUE, sub="PREV", big=True)
    key(sc, 132, 312, 104, 104, "MAIN", TEXT, sub="EXIT")
    key(sc, 248, 312, 104, 104, "▶", BLUE, sub="NEXT", big=True)
    return sc


def reset_screen(progress=0.6):
    sc = Screen()
    header(sc, "FACTORY RESET", 8, 8)
    sc.text(184, 92, "Erase callsign and settings", font(MONT, 14), TEXT, "mm")
    sc.text(184, 114, "Hold the switch for 2 seconds", font(MONT, 13), FRAME, "mm")
    for i in range(20):
        sc.rect(46 + i * 14, 138, 56 + i * 14, 160, fill=RED if i < int(progress * 20) else DIM)
    notched_frame(sc, 16, 60, 352, 184)
    housing(sc, 16, 196, 336, 104)
    sc.vgrad(30, 210, 338, 286, "#4A1014", "#1E0507")
    sc.glow_rect(30, 210, 338, 286, RED, 140, ring=True)
    sc.line([(30, 286), (30, 210), (338, 210)], "#8A3A3E", 1)
    sc.text(184, 240, "HOLD TO ERASE", font(MONT, 20), "#F0C8C8", "mm")
    sc.text(184, 266, f"ARMING {int(progress * 100)}%", font(MONT, 12), "#E09090", "mm")
    key(sc, 16, 312, 104, 104, "◀", BLUE, sub="PREV", big=True)
    key(sc, 132, 312, 104, 104, "MAIN", TEXT, sub="EXIT")
    key(sc, 248, 312, 104, 104, "▶", BLUE, sub="NEXT", big=True)
    return sc


CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-"


def callsign_screen(cs="TK-421", sel=3):
    sc = Screen()
    sc.text(46, 28, "CALLSIGN", font(MONT, 22), TEXT, "lm")
    sc.text(322, 28, "CODE CYLINDER", font(MONT, 11), FRAME, "rm")
    n, dw, gap = 10, 29, 3
    x0 = (W - (n * dw + (n - 1) * gap)) / 2
    for i in range(n):
        c = cs[i] if i < len(cs) else " "
        k = CHARS.index(c)
        x = x0 + i * (dw + gap)
        sc.vgrad(x, 58, x + dw, 146, "#05070A", "#05070A")
        sc.vgrad(x, 58, x + dw, 80, "#000000", "#1A1F24")
        sc.vgrad(x, 124, x + dw, 146, "#1A1F24", "#000000")
        sc.rect(x, 86, x + dw, 118, fill="#1A1F24")
        f = font(MONT, 18)
        show = lambda ch: "·" if ch == " " else ch
        sc.text(x + dw / 2, 70, show(CHARS[(k - 1) % len(CHARS)]), font(MONT, 13), "#3E4448", "mm")
        sc.text(x + dw / 2, 102, show(c), f, AMBER if i == sel else TEXT, "mm")
        sc.text(x + dw / 2, 134, show(CHARS[(k + 1) % len(CHARS)]), font(MONT, 13), "#3E4448", "mm")
        sc.rect(x, 58, x + dw, 146, outline=AMBER if i == sel else "#2A2F34", width=2 if i == sel else 1)
        if i == sel:
            sc.d.polygon(sc.b(x + dw / 2 - 6, 158, x + dw / 2 + 6, 158, x + dw / 2, 150), fill=AMBER)
    sc.text(184, 174, cs, font(AURENG, 20), RED, "mm")
    key(sc, 16, 196, 160, 104, "▲", AMBER, sub="NEXT CHAR", big=True)
    key(sc, 192, 196, 160, 104, "▼", AMBER, sub="PREV CHAR", big=True)
    key(sc, 16, 312, 104, 104, "◀", BLUE, sub="POSITION", big=True)
    key(sc, 132, 312, 104, 104, "SAVE", GREEN, lit=True, sub="COMMIT")
    key(sc, 248, 312, 104, 104, "▶", BLUE, sub="POSITION", big=True)
    return sc


# ---------------- output ----------------
def main():
    out = HERE
    shots = [
        ("01_main_muted", main_screen(live=False).finish(), "Main · muted (boot state)"),
        ("02_main_live_cover_open", main_screen(live=True, cover_open=True, level=78).finish(),
         "Main · LIVE lit, lock cover flipped open"),
        ("03_settings_effect", settings_screen().finish(), "Settings · one value per page"),
        ("04_callsign", callsign_screen().finish(), "Callsign · code cylinder"),
        ("05_settings_reset", reset_screen().finish(), "Settings · hold-to-erase reset"),
        ("06_lock", lock_screen().finish(), "Lock · prop mode, 5 taps to unlock"),
    ]
    for name, im, _ in shots:
        im.save(os.path.join(out, name + ".png"))

    # contact sheet
    tw, th = shots[0][1].size
    sheet = Image.new("RGB", (tw * 3 + 80, (th + 70) * 2 + 40), "#0D0F11")
    sd = ImageDraw.Draw(sheet)
    for i, (_, im, cap) in enumerate(shots):
        x, y = 20 + (i % 3) * (tw + 20), 20 + (i // 3) * (th + 70)
        sheet.paste(im, (x, y))
        sd.text((x + 10, y + th + 14), cap, font=ImageFont.truetype(MONT, 30), fill="#C9CDD0")
    sheet.save(os.path.join(out, "00_contact_sheet.png"))

    # animated main screen: lamps drift + red pulse fade, scomp link spins, scope scrolls
    rnd = random.Random(7)
    states = ["blue", "grey", "orange", "grey", "red", "blue", "orange"]
    frames = []
    for f in range(48):
        if f % 12 == 11:
            j = rnd.randrange(7)
            states[j] = rnd.choice([c for c in LAMP if c != states[j]])
        opas = [(0.45 + 0.55 * abs(math.sin((f + i * 5) / 7.6))) if s == "red" else 1 for i, s in enumerate(states)]
        im = main_screen(live=f >= 24, t=f, lamp_states=list(states), lamp_opas=opas,
                         level=int(40 + 35 * abs(math.sin(f / 5)))).finish()
        frames.append(im.resize((im.width // 2, im.height // 2), Image.LANCZOS))
    frames[0].save(os.path.join(out, "main_alive.gif"), save_all=True, append_images=frames[1:],
                   duration=110, loop=0)
    print("wrote", len(shots), "screens, contact sheet, main_alive.gif to", out)


if __name__ == "__main__":
    main()
