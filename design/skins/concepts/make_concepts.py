"""20 Imperial-style skin concepts for the 368x448 AMOLED console.

Every concept keeps the same functions (callsign, TX/CLIP, voice scope + tilt lines,
voice meter, battery bar, lamps, Aurebesh readout with the secret temp, a rotor, and the
LOCK / SYS / LIVE keys in the same bottom-row positions) but remixes the look: palette,
scope frame, meter type, lamp style, rotor design, key style and layout.

    python3 design/skins/concepts/make_concepts.py      (needs Pillow)
writes design/skins/concepts/NN_name.png and sheet_1.png / sheet_2.png
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "mockups"))
import make_mockups as m  # noqa: E402

S = m.S
f = m.font

# ---------------- palettes ----------------
PAL = {
    "standard":  dict(frame="#8A8A8A", grid="#1E2429", trace="#459FC2", accent="#C4262E", text="#C9CDD0",
                      tick="#916B19", lamps=["#979B9A", "#BF5B16", "#D00A1B", "#459FC2"], key="#459FC2"),
    "targeting": dict(frame="#7A1A1A", grid="#2A0C0C", trace="#E0402E", accent="#E0402E", text="#E8B0A0",
                      tick="#9A4A20", lamps=["#7A1A1A", "#E0A526", "#E0402E", "#5A2020"], key="#E0A526"),
    "deathstar": dict(frame="#6C7A88", grid="#18212A", trace="#5FC8E8", accent="#C4262E", text="#D0DCE6",
                      tick="#4F7F99", lamps=["#C9D3DC", "#5FC8E8", "#C4262E", "#3C8A44"], key="#5FC8E8"),
    "bridge":    dict(frame="#2F6B3A", grid="#0E2414", trace="#6CE07A", accent="#E0A526", text="#B8E8C0",
                      tick="#3F8F4A", lamps=["#6CE07A", "#2F6B3A", "#E0A526", "#C4262E"], key="#6CE07A"),
    "isb":       dict(frame="#5E7470", grid="#142320", trace="#7FB8A8", accent="#D8742A", text="#C8D8D4",
                      tick="#7A6040", lamps=["#7FB8A8", "#D8742A", "#4A5C58", "#C8D8D4"], key="#7FB8A8"),
    "amber":     dict(frame="#8A6418", grid="#231A06", trace="#F0B030", accent="#F0B030", text="#F0D090",
                      tick="#9A7020", lamps=["#F0B030", "#8A6418", "#C46A10", "#4A3810"], key="#F0B030"),
    "crimson":   dict(frame="#6E1418", grid="#200608", trace="#E04040", accent="#E04040", text="#E8C8C8",
                      tick="#8A3030", lamps=["#B01820", "#6E1418", "#E8E8E8", "#3A1012"], key="#E8C8C8"),
    "stealth":   dict(frame="#3A3F44", grid="#111417", trace="#5A6A78", accent="#8A2A2E", text="#8A9298",
                      tick="#4A4A3A", lamps=["#3A3F44", "#5A6A78", "#8A2A2E", "#2A2F34"], key="#5A6A78"),
    "tie":       dict(frame="#4A5A70", grid="#121A24", trace="#9FB7D8", accent="#C8D4E4", text="#C8D4E4",
                      tick="#5A6A88", lamps=["#9FB7D8", "#4A5A70", "#C8D4E4", "#D00A1B"], key="#9FB7D8"),
    "vector":    dict(frame="#B0B0B0", grid="#262626", trace="#E8E8E8", accent="#C4262E", text="#D8D8D8",
                      tick="#6A6A6A", lamps=["#E8E8E8", "#8A8A8A", "#4A4A4A", "#C4262E"], key="#D8D8D8"),
    "retro":     dict(frame="#3F8F9A", grid="#0C2226", trace="#4FD1D9", accent="#F07A2A", text="#BFEFF2",
                      tick="#2F7F88", lamps=["#4FD1D9", "#F07A2A", "#F0C040", "#2A4F55"], key="#4FD1D9"),
    "command":   dict(frame="#707880", grid="#181C20", trace="#C8CED4", accent="#E07A1A", text="#D8DCE0",
                      tick="#8A6A40", lamps=["#C8CED4", "#E07A1A", "#707880", "#C4262E"], key="#E07A1A"),
    "vault":     dict(frame="#3C7A80", grid="#0E2022", trace="#58D0C0", accent="#E0A526", text="#B8E0DC",
                      tick="#3C7A70", lamps=["#58D0C0", "#E0A526", "#3C7A80", "#C9D3DC"], key="#58D0C0"),
}

BATT = ("#3C8A44", "#E0A526", "#C4262E")


def hexa(c, a):
    return m.hexrgb(c, a)


# ---------------------------------------------------------------------------------------
# Rendering modes. One description per concept drives three outputs:
#   "full"   the mock-up (static + live parts, as on the device)
#   "static" the background layer (skin) for the main screen: everything that never moves
#   "lock"   the background layer for the lock screen (same, with plates instead of keys)
# The live parts are drawn by the device code from the same geometry (skins.json).
# ---------------------------------------------------------------------------------------
STATIC = False      # True while drawing a background layer: skip everything live


# ---------------- scope frames ----------------
def frame_notched(sc, b, p):
    m.notched_frame(sc, b[0] - 3, b[1] - 3, b[2] + 3, b[3] + 3, p["frame"])


def frame_brackets(sc, b, p, L=16):
    x0, y0, x1, y1 = b[0] - 3, b[1] - 3, b[2] + 3, b[3] + 3
    for (cx, cy, dx, dy) in ((x0, y0, 1, 1), (x1, y0, -1, 1), (x0, y1, 1, -1), (x1, y1, -1, -1)):
        sc.line([(cx + dx * L, cy), (cx, cy), (cx, cy + dy * L)], p["frame"], 2)


def double_pts(b, k):
    x0, y0, x1, y1 = b[0] - 3, b[1] - 3, b[2] + 3, b[3] + 3
    c = 10 - k
    return [(x0 + k + c, y0 + k), (x1 - k - c, y0 + k), (x1 - k, y0 + k + c), (x1 - k, y1 - k - c),
            (x1 - k - c, y1 - k), (x0 + k + c, y1 - k), (x0 + k, y1 - k - c), (x0 + k, y0 + k + c),
            (x0 + k + c, y0 + k)]


def frame_double(sc, b, p):
    for k, w in ((0, 2), (5, 1)):
        sc.line(double_pts(b, k), p["frame"], w)


def frame_ticks(sc, b, p):
    x0, y0, x1, y1 = b[0] - 3, b[1] - 3, b[2] + 3, b[3] + 3
    sc.rect(x0, y0, x1, y1, outline=p["frame"], width=1)
    for i in range(0, 41):
        x = x0 + (x1 - x0) * i / 40
        sc.line([(x, y1), (x, y1 + (7 if i % 5 == 0 else 3))], p["frame"], 1)
    for i in range(0, 11):
        y = y0 + (y1 - y0) * i / 10
        sc.line([(x0, y), (x0 - (6 if i % 5 == 0 else 3), y)], p["frame"], 1)


def frame_dashed(sc, b, p):
    x0, y0, x1, y1 = b[0] - 3, b[1] - 3, b[2] + 3, b[3] + 3
    for x in range(int(x0), int(x1), 12):
        sc.line([(x, y0), (min(x + 7, x1), y0)], p["frame"], 2)
        sc.line([(x, y1), (min(x + 7, x1), y1)], p["frame"], 2)
    for y in range(int(y0), int(y1), 12):
        sc.line([(x0, y), (x0, min(y + 7, y1))], p["frame"], 2)
        sc.line([(x1, y), (x1, min(y + 7, y1))], p["frame"], 2)


FRAMES = dict(notched=frame_notched, brackets=frame_brackets, double=frame_double, ticks=frame_ticks,
              dashed=frame_dashed)


def tilt_col(p):
    return m.mix(p["grid"], "#FFFFFF", 0.18)


def scope_rect(sc, b, p, frame, t, tilt=(12, -6)):
    x0, y0, x1, y1 = b
    sc.rect(x0, y0, x1, y1, fill="#06080A")
    for k in range(1, 4):
        sc.line([(x0, y0 + (y1 - y0) * k / 4), (x1, y0 + (y1 - y0) * k / 4)], p["grid"], 1)
    for k in range(1, 6):
        sc.line([(x0 + (x1 - x0) * k / 6, y0), (x0 + (x1 - x0) * k / 6, y1)], p["grid"], 1)
    FRAMES[frame](sc, b, p)
    if STATIC:
        return
    tx = (x0 + x1) / 2 + tilt[0] / 45 * (x1 - x0 - 6) / 2
    ty = (y0 + y1) / 2 + tilt[1] / 45 * (y1 - y0 - 6) / 2
    sc.line([(tx, y0 + 2), (tx, y1 - 2)], tilt_col(p), 1)
    sc.line([(x0 + 2, ty), (x1 - 2, ty)], tilt_col(p), 1)
    wave = m.voice_wave(60, 3 + t, t)
    pts = [(x0 + 3 + (x1 - x0 - 6) * i / 59, y1 - 4 - (y1 - y0 - 8) * v / 100) for i, v in enumerate(wave)]
    sc.line(pts, p["trace"], 2)


def scope_round(sc, cx, cy, r, p, t, tilt=(12, -6)):
    """round targeting scope: concentric rings, crosshair, range ticks; trace across the middle"""
    sc.d.ellipse(sc.b(cx - r, cy - r, cx + r, cy + r), fill="#06080A", outline=p["frame"], width=2 * S)
    for k in (0.33, 0.66):
        sc.d.ellipse(sc.b(cx - r * k, cy - r * k, cx + r * k, cy + r * k), outline=p["grid"], width=S)
    sc.line([(cx - r, cy), (cx + r, cy)], p["grid"], 1)
    sc.line([(cx, cy - r), (cx, cy + r)], p["grid"], 1)
    for a in range(0, 360, 10):
        L = 7 if a % 30 == 0 else 3
        ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
        sc.line([(cx + (r + 2) * ca, cy + (r + 2) * sa), (cx + (r + 2 + L) * ca, cy + (r + 2 + L) * sa)], p["frame"], 1)
    if STATIC:
        return
    tx, ty = cx + tilt[0] / 45 * r * 0.75, cy + tilt[1] / 45 * r * 0.75
    sc.line([(tx, cy - r * 0.75), (tx, cy + r * 0.75)], tilt_col(p), 1)
    sc.line([(cx - r * 0.75, ty), (cx + r * 0.75, ty)], tilt_col(p), 1)
    wave = m.voice_wave(50, 5 + t, t)
    pts = [(cx - r * 0.8 + 1.6 * r * i / 49, cy + r * 0.35 - (v / 100) * r * 0.9) for i, v in enumerate(wave)]
    sc.line(pts, p["trace"], 2)


# ---------------- voice meters (wells are static, lit parts live) ----------------
WELL = "#0B0E11"


def meter_vseg(sc, x, y0, y1, p, level, w=36, n=12):
    h = (y1 - y0) / n
    for i in range(n):
        lit = (not STATIC) and i < level * n // 100
        c = (p["accent"] if i >= n * 0.75 else BATT[0]) if lit else WELL
        sc.rect(x, y1 - (i + 1) * h + 2, x + w, y1 - i * h - 1, fill=c)


def meter_hseg(sc, x0, x1, y, p, level, h=10, n=24):
    w = (x1 - x0) / n
    for i in range(n):
        lit = (not STATIC) and i < level * n // 100
        c = (p["accent"] if i >= n * 0.75 else p["trace"]) if lit else WELL
        sc.rect(x0 + i * w + 1, y, x0 + (i + 1) * w - 2, y + h, fill=c)


def meter_dots(sc, x, y0, y1, p, level, n=11):
    step = (y1 - y0) / n
    for i in range(n):
        lit = (not STATIC) and i < level * n // 100
        cy = y1 - step * (i + 0.5)
        r = step * 0.36
        c = (p["accent"] if i >= n * 0.75 else p["trace"]) if lit else "#101418"
        sc.d.ellipse(sc.b(x - r, cy - r, x + r, cy + r), fill=c)


def meter_twin(sc, x, y0, y1, p, level):
    for k, lv in ((0, level), (16, max(0, level - 12))):
        sc.rect(x + k, y0, x + k + 10, y1, fill=WELL)
        if not STATIC:
            hh = (y1 - y0) * lv / 100
            sc.vgrad(x + k, y1 - hh, x + k + 10, y1, p["accent"], p["trace"])


def meter_arc(sc, cx, cy, r, p, level):
    sc.arc(cx, cy, r, 200, 340, "#1A1F24", 8)
    for a in range(200, 341, 14):
        ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
        sc.line([(cx + (r + 3) * ca, cy + (r + 3) * sa), (cx + (r + 8) * ca, cy + (r + 8) * sa)], p["frame"], 1)
    if STATIC:
        return
    sc.arc(cx, cy, r, 200, 200 + 140 * level / 100, p["trace"], 8)
    a = math.radians(200 + 140 * level / 100)
    sc.line([(cx, cy), (cx + (r - 6) * math.cos(a), cy + (r - 6) * math.sin(a))], p["accent"], 2)
    sc.d.ellipse(sc.b(cx - 4, cy - 4, cx + 4, cy + 4), fill=p["frame"])


def batt_v(sc, x, y0, y1, pct=78, w=8):
    sc.rect(x, y0, x + w, y1, fill="#07090B")
    if not STATIC:
        h = (y1 - y0) * pct / 100
        sc.rect(x, y1 - h, x + w, y1, fill=BATT[2] if pct <= 15 else BATT[1] if pct <= 30 else BATT[0])


def batt_h(sc, x0, x1, y, pct=78, h=5):
    sc.rect(x0, y, x1, y + h, fill="#07090B")
    if not STATIC:
        w = (x1 - x0) * pct / 100
        sc.rect(x0, y, x0 + w, y + h, fill=BATT[2] if pct <= 15 else BATT[1] if pct <= 30 else BATT[0])


# ---------------- lamp styles (lamps are live; bezels / tubes static) ----------------
def lamp_states(n, seed):
    rnd = random.Random(seed)
    return [rnd.randrange(4) for _ in range(n)]


def glowdot(sc, box, col, dome):
    if dome:
        sc.d.ellipse(sc.b(*box), fill=col)
        x0, y0, x1, y1 = box
        sc.d.ellipse(sc.b(x0 + (x1 - x0) * 0.2, y0 + (y1 - y0) * 0.18, x0 + (x1 - x0) * 0.42,
                          y0 + (y1 - y0) * 0.38), fill=m.mix(col, "#FFFFFF", 0.4))
    else:
        sc.rect(*box, fill=col)


OFF = "#151A1F"     # unlit lamp in banks that have an off state (switcher, matrix)


def lamps_grid(sc, cx, cy, p, seed):
    if STATIC:
        return
    st = lamp_states(7, seed)
    x, y = cx - 40, cy - 54
    for i, (lx, ly, lw, lh, dome) in enumerate(m.LAMP_LAYOUT):
        glowdot(sc, (x + lx, y + ly, x + lx + lw, y + ly + lh), p["lamps"][st[i]], dome)


def switcher_geom(cx, cy, cols, rows):
    w, h, gx, gy = 9, 7, 4, 7
    return w, h, gx, gy, cx - (cols * (w + gx) - gx) / 2, cy - (rows * (h + gy) - gy) / 2


def lamps_switcher(sc, cx, cy, p, seed, cols=8, rows=5):
    """broadcast-switcher bank: many small unlabelled lamps"""
    if STATIC:
        return
    st = lamp_states(cols * rows, seed)
    w, h, gx, gy, x0, y0 = switcher_geom(cx, cy, cols, rows)
    for r in range(rows):
        for c in range(cols):
            s_ = st[r * cols + c]
            col = p["lamps"][s_] if s_ != 0 else OFF
            sc.rect(x0 + c * (w + gx), y0 + r * (h + gy), x0 + c * (w + gx) + w, y0 + r * (h + gy) + h, fill=col)


def lamps_domes(sc, cx, cy, p, seed, cols=3, rows=3, r=11):
    st = lamp_states(cols * rows, seed)
    pitch = r * 2 + 8
    x0 = cx - (cols - 1) * pitch / 2
    y0 = cy - (rows - 1) * pitch / 2
    for k in range(cols * rows):
        x, y = x0 + (k % cols) * pitch, y0 + (k // cols) * pitch
        sc.d.ellipse(sc.b(x - r - 2, y - r - 2, x + r + 2, y + r + 2), fill="#1A1F24")
        if not STATIC:
            glowdot(sc, (x - r, y - r, x + r, y + r), p["lamps"][st[k]], True)


PILL_H = (34, 52, 70, 34, 52)


def lamps_pills(sc, cx, cy, p, seed, n=5):
    st = lamp_states(n, seed)
    for i in range(n):
        x = cx - (n - 1) * 8 + i * 16
        sc.d.rounded_rectangle(sc.b(x - 5, cy - 50, x + 5, cy + 46), radius=5 * S, fill=OFF)
        if not STATIC:
            sc.d.rounded_rectangle(sc.b(x - 5, cy + 46 - PILL_H[i], x + 5, cy + 46), radius=5 * S, fill=p["lamps"][st[i]])


def lamps_diamonds(sc, cx, cy, p, seed):
    if STATIC:
        return
    st = lamp_states(9, seed)
    for k in range(9):
        x = cx + ((k % 3) - 1) * 26 + (13 if (k // 3) % 2 else 0) - 6
        y = cy + ((k // 3) - 1) * 28
        r = 10
        sc.d.polygon(sc.b(x, y - r, x + r, y, x, y + r, x - r, y), fill=p["lamps"][st[k]])


def lamps_matrix(sc, cx, cy, p, seed, n=4):
    if STATIC:
        return
    st = lamp_states(n * n, seed)
    s_, g = 16, 6
    x0 = cx - (n * (s_ + g) - g) / 2
    y0 = cy - (n * (s_ + g) - g) / 2
    for k in range(n * n):
        x, y = x0 + (k % n) * (s_ + g), y0 + (k // n) * (s_ + g)
        sc.rect(x, y, x + s_, y + s_, fill=p["lamps"][st[k]] if st[k] != 0 else OFF)


LAMPS = dict(grid=lamps_grid, switcher=lamps_switcher, domes=lamps_domes, pills=lamps_pills,
             diamonds=lamps_diamonds, matrix=lamps_matrix)


# ---------------- rotors (sockets / plates static, moving parts live) ----------------
def rotor_scomp(sc, cx, cy, p, t, r=46):
    sc.d.ellipse(sc.b(cx - r - 6, cy - r - 6, cx + r + 6, cy + r + 6), outline="#22272B", width=2 * S)
    sc.d.ellipse(sc.b(cx - 7, cy - 7, cx + 7, cy + 7), fill="#B9BEC2")
    if STATIC:
        return
    o = 37 * t
    for i in range(3):
        sc.arc(cx, cy, r - 4, o + i * 120, o + i * 120 + 80, m.mix(p["frame"], "#FFFFFF", 0.1), 8)
    sc.arc(cx, cy, r - 18, -73 * t, -73 * t + 250, p["frame"], 7)
    sc.arc(cx, cy, r - 30, 160 * t, 160 * t + 60, p["trace"], 4)
    sc.arc(cx, cy, r - 30, 160 * t + 180, 160 * t + 240, p["trace"], 4)


def rotor_reticle(sc, cx, cy, p, t, r=46):
    for k in (1.0, 0.7, 0.4):
        sc.d.ellipse(sc.b(cx - r * k, cy - r * k, cx + r * k, cy + r * k), outline=p["frame"], width=S)
    for a in range(0, 360, 15):
        ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
        L = 8 if a % 45 == 0 else 4
        sc.line([(cx + r * ca, cy + r * sa), (cx + (r - L) * ca, cy + (r - L) * sa)], p["frame"], 1)
    if STATIC:
        return
    for dx, dy in ((14, -18), (-22, 10)):                    # blips drift slowly (live)
        dx, dy = dx * r / 46, dy * r / 46
        sc.d.ellipse(sc.b(cx + dx - 3, cy + dy - 3, cx + dx + 3, cy + dy + 3), fill=p["accent"])
    a = math.radians(70 * t)
    for g in (3, 2, 1):                                      # fading sweep trail
        b = a - math.radians(g * 6)
        sc.line([(cx, cy), (cx + r * math.cos(b), cy + r * math.sin(b))],
                m.mix("#000000", p["trace"], 0.5 * (1 - g / 4)), 1)
    sc.line([(cx, cy), (cx + r * math.cos(a), cy + r * math.sin(a))], p["trace"], 2)
    sc.arc(cx, cy, r + 6, 40 * t, 40 * t + 50, p["accent"], 3)


def square_pts(cx, cy, rr, ang):
    """4 L-shaped corner brackets of a square rotated by ang (degrees), 3 points each"""
    a = math.radians(ang + 45)
    pts = [(cx + rr * math.cos(a + math.pi / 2 * i), cy + rr * math.sin(a + math.pi / 2 * i)) for i in range(4)]
    out = []
    for i in range(4):
        prev, cur, nxt = pts[i - 1], pts[i], pts[(i + 1) % 4]
        out.append([(cur[0] + (prev[0] - cur[0]) * 0.3, cur[1] + (prev[1] - cur[1]) * 0.3), cur,
                    (cur[0] + (nxt[0] - cur[0]) * 0.3, cur[1] + (nxt[1] - cur[1]) * 0.3)])
    return out


def rotor_square(sc, cx, cy, p, t, r=44):
    sc.rect(cx - 4, cy - 4, cx + 4, cy + 4, fill=p["text"])
    if STATIC:
        return
    for k, (spd, col, w) in enumerate(((20, p["frame"], 4), (-35, p["trace"], 3), (60, p["accent"], 2))):
        for br in square_pts(cx, cy, r - k * 13, spd * t):
            sc.line(br, col, w)


def rotor_twin(sc, cx, cy, p, t, r=None):
    for dx in (-24, 24):
        x = cx + dx
        sc.d.ellipse(sc.b(x - 22, cy - 22, x + 22, cy + 22), outline="#22272B", width=2 * S)
        sc.d.ellipse(sc.b(x - 3, cy - 3, x + 3, cy + 3), fill=p["text"])
    sc.rect(cx - 30, cy + 34, cx + 30, cy + 40, fill=p["frame"])
    if STATIC:
        return
    for dx, sgn in ((-24, 1), (24, -1)):
        x = cx + dx
        for i in range(3):
            o = sgn * 50 * t + i * 120
            sc.arc(x, cy, 18, o, o + 70, m.mix(p["frame"], "#FFFFFF", 0.1), 6)
        sc.arc(x, cy, 9, -sgn * 120 * t, -sgn * 120 * t + 90, p["trace"], 3)


def rotor_radar(sc, cx, cy, p, t, r=46):
    sc.d.ellipse(sc.b(cx - r, cy - r, cx + r, cy + r), fill="#05090A", outline=p["frame"], width=2 * S)
    for k in (0.33, 0.66):
        sc.d.ellipse(sc.b(cx - r * k, cy - r * k, cx + r * k, cy + r * k), outline=p["grid"], width=S)
    if STATIC:
        return
    for dx, dy in ((18, -14), (-12, 22), (-26, -8)):         # blips drift slowly (live)
        dx, dy = dx * r / 46, dy * r / 46
        sc.d.ellipse(sc.b(cx + dx - 3, cy + dy - 3, cx + dx + 3, cy + dy + 3), fill=p["accent"])
    a0 = 80 * t
    layer = Image.new("RGBA", sc.img.size, (0, 0, 0, 0))
    ImageDraw.Draw(layer).pieslice(sc.b(cx - r + 2, cy - r + 2, cx + r - 2, cy + r - 2), a0 - 40, a0,
                                   fill=hexa(p["trace"], 70))
    sc.img.alpha_composite(layer)
    sc.d = ImageDraw.Draw(sc.img)
    a = math.radians(a0)
    sc.line([(cx, cy), (cx + (r - 2) * math.cos(a), cy + (r - 2) * math.sin(a))], p["trace"], 2)


def gear_pts(cx, cy, r, ang, teeth=16):
    o = math.radians(ang)
    pts = []
    for i in range(teeth * 2 + 1):
        a = o + math.pi * i / teeth
        rr = r if i % 2 == 0 else r - 7
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    return pts


def rotor_gear(sc, cx, cy, p, t, r=44):
    sc.d.ellipse(sc.b(cx - 6, cy - 6, cx + 6, cy + 6), fill=p["text"])
    if STATIC:
        return
    sc.line(gear_pts(cx, cy, r, 25 * t), p["frame"], 2)
    sc.arc(cx, cy, r - 14, -60 * t, -60 * t + 200, p["trace"], 6)
    sc.arc(cx, cy, r - 26, 120 * t, 120 * t + 90, p["accent"], 4)


ROTORS = dict(scomp=rotor_scomp, reticle=rotor_reticle, square=rotor_square, twin=rotor_twin,
              radar=rotor_radar, gear=rotor_gear)


# ---------------- readout + top bar ----------------
WORDS = ["UPLINK", "SECTOR", "BEACON", "CIPHER", "PATROL", "RELAY", "VECTOR", "ORBIT", "SIGNAL"]


def readout_rows(cy, lines, pitch):
    y0 = cy - ((lines - 1) * pitch) // 2          # same integer maths as the device code
    return [y0 + i * pitch for i in range(lines)]


def readout(sc, cx, cy, p, seed, lines=4, pitch=29):
    if STATIC:
        return
    rnd = random.Random(seed)
    fw, fn = f(m.AUREB, 13), f(m.AURENG, 18)
    rows = readout_rows(cy, lines, pitch)
    track, stretch = 0.22, 1.15
    w_word = (sum(fw.getlength(ch) for ch in "TEMP") + track * fw.size * 3) * stretch / S
    w_num = fn.getlength("88") / S
    w_unit = fw.getlength("F") * stretch / S
    x = cx - (w_word + 6 + w_num + 4 + w_unit) / 2
    base = rows[0] + 6
    sc.text(x, base, "TEMP", fw, p["tick"], "ls")
    sc.text(x + w_word + 6, base, "88", fn, p["tick"], "ls")
    sc.text(x + w_word + 6 + w_num + 4, base, "F", fw, p["tick"], "ls")
    for i in range(1, lines):
        sc.text(cx, rows[i], rnd.choice(WORDS), fw, p["tick"], "mm")


def top_bar(sc, p, lock=False):
    lf = f(m.AUREB, 12) if lock else f(m.MONT, 13)
    for x, label in ((276, "TX"), (340, "CLIP")):
        sc.text(x - 5, 29, label, lf, m.mix(p["text"], "#000000", 0.2), "rm")
    if STATIC:
        return
    sc.text(46, 31, "TK-421", f(m.AURENG, 33), p["accent"], "lm")   # = 42 px on the device
    for x, on in ((276, True), (340, False)):
        sc.rect(x, 22, x + 14, 36, fill=p["accent"] if on else "#0C0F12", outline=p["frame"])
        if on:
            sc.glow_rect(x, 22, x + 14, 36, p["accent"], 140)


# ---------------- keys ----------------
def lock_housing(sc):
    """LOCK: guarded toggle on every skin (housing + switch nut static, cover + lever live)"""
    m.housing(sc, 16, 320, 104, 114)
    cx, cy = 68, 383
    sc.d.ellipse(sc.b(cx - 20, cy - 20, cx + 20, cy + 20), fill="#2A3036", outline="#6A7177", width=2 * S)
    sc.d.ellipse(sc.b(cx - 11, cy - 11, cx + 11, cy + 11), fill="#151A1E", outline="#4C5358", width=S)
    sc.text(68, 421, "LOCK", f(m.MONT, 13), m.LABEL, "mm")


def keys(sc, p, style):
    y, h = 320, 114
    if STATIC:
        lock_housing(sc)
        if style != "flat":
            for x, col in ((132, p["key"]), (248, p["accent"])):
                m.housing(sc, x, y, 104, h)
                sc.d.rounded_rectangle(sc.b(x + 11, y + 11, x + 93, y + 103), radius=m.CAP_R * S, fill="#07090B")
                if style == "ring":
                    sc.d.rounded_rectangle(sc.b(x + 6, y + 6, x + 98, y + 108), radius=10 * S, outline=col, width=2 * S)
        return
    m.guarded_toggle(sc, 16, y, 104, h)
    if style == "flat":
        for x, lbl, sub, col, lit in ((132, "SYS", "SETTINGS", p["key"], False),
                                     (248, "LIVE", "TRANSMIT", p["accent"], True)):
            if lit:
                sc.d.rounded_rectangle(sc.b(x + 4, y + 4, x + 100, y + 110), radius=8 * S, fill=m.mix("#000000", col, 0.35))
                sc.glow_rect(x + 4, y + 4, x + 100, y + 110, col, 160, ring=True)
            sc.d.rounded_rectangle(sc.b(x + 4, y + 4, x + 100, y + 110), radius=8 * S, outline=col, width=2 * S)
            sc.text(x + 52, y + 50, lbl, f(m.MONT, 18), "#FFF4F0" if lit else col, "mm")
            sc.text(x + 52, y + 74, sub, f(m.MONT, 13), m.mix(p["text"], "#000000", 0.25), "mm")
        return
    m.key(sc, 132, y, 104, h, "SYS", p["key"], sub="SETTINGS")
    m.key(sc, 248, y, 104, h, "LIVE", p["accent"], lit=True, sub="TRANSMIT")
    if style == "strip":
        for x, col in ((132, p["key"]), (248, p["accent"])):
            sc.d.rounded_rectangle(sc.b(x + 13, y + 13, x + 91, y + 20), radius=2 * S, fill=col)
    elif style == "ring":
        for x, col in ((132, p["key"]), (248, p["accent"])):
            sc.d.rounded_rectangle(sc.b(x + 6, y + 6, x + 98, y + 108), radius=10 * S, outline=col, width=2 * S)
            sc.glow_rect(x + 6, y + 6, x + 98, y + 108, col, 90, ring=True)
    elif style == "pill":
        for x, col in ((132, p["key"]), (248, p["accent"])):
            sc.d.ellipse(sc.b(x + 44, y + 92, x + 60, y + 100), fill=col)


def lock_plates(sc, p):
    """lock screen: the key positions become read-only plates (static); bar/hex/bars live"""
    for x in (16, 132, 248):
        m.housing(sc, x, 320, 104, 114)
    sc.text(68, 361, "LOCKED", f(m.AUREB, 15), p["accent"], "mm")
    sc.text(184, 346, "ENC", f(m.AUREB, 12), p["frame"], "mm")
    sc.text(300, 342, "SIG", f(m.AUREB, 12), p["frame"], "mm")


# ---------------- geometry: one table per layout (shared with the device via skins.json) ----------------
def geom(c):
    L, meter = c["layout"], c.get("meter", "vseg")
    if L == "standard":
        g = dict(scope=("rect", 18, 62, 288, 176), batt=("v", 346, 49, 178, 8), lamps=(68, 250),
                 readout=(184, 246, 4, 29), rotor=(300, 250, 46),
                 meter={"vseg": ("vseg", 306, 49, 178, 36, 12), "dots": ("dots", 322, 46, 180, 11, 0),
                        "twin": ("twin", 306, 49, 178, 0, 0)}[meter])
    elif L == "wide":
        g = dict(scope=("rect", 18, 62, 350, 156), meter=("hseg", 18, 350, 170, 10, 24),
                 batt=("h", 18, 350, 186, 5), lamps=(68, 252), readout=(184, 248, 4, 29), rotor=(300, 252, 46))
    elif L == "round":
        g = dict(scope=("round", 184, 122, 66, 0), batt=("v", 340, 60, 196, 10), lamps=(68, 258),
                 readout=(184, 258, 3, 30), rotor=(300, 258, 46),
                 meter=("dots", 34, 56, 196, 11, 0) if meter == "dots" else ("vseg", 18, 60, 196, 26, 12))
    elif L == "hero":
        g = dict(scope=("rect", 166, 66, 350, 190), meter=("arc", 184, 282, 44, 0, 0),
                 batt=("h", 166, 350, 199, 5), lamps=(68, 258), readout=(300, 260, 3, 26), rotor=(84, 128, 62))
    else:  # mirror
        g = dict(scope=("rect", 76, 62, 350, 176), batt=("v", 50, 49, 178, 8), lamps=(300, 250),
                 readout=(184, 246, 4, 29), rotor=(68, 250, 46),
                 meter=("vseg", 18, 49, 178, 24, 12) if meter == "vseg" else ("twin", 18, 49, 178, 0, 0))
    if c["lamps"] == "switcher":
        g["switcher"] = (6, 4) if L == "hero" else (8, 5)
        if L == "hero":
            g["lamps"] = (68, 258)          # over LOCK, level with the gauge + readout
    return g


def draw_console(sc, c, t, lock=False):
    p = PAL[c["pal"]]
    g = geom(c)
    top_bar(sc, p, lock)
    sk = g["scope"]
    if sk[0] == "rect":
        scope_rect(sc, sk[1:5], p, c["frame"], t)
    else:
        scope_round(sc, sk[1], sk[2], sk[3], p, t)
    mk = g["meter"]
    {"vseg": lambda: meter_vseg(sc, mk[1], mk[2], mk[3], p, 66, w=mk[4], n=mk[5]),
     "dots": lambda: meter_dots(sc, mk[1], mk[2], mk[3], p, 66, n=mk[4]),
     "twin": lambda: meter_twin(sc, mk[1], mk[2], mk[3], p, 70),
     "hseg": lambda: meter_hseg(sc, mk[1], mk[2], mk[3], p, 62, h=mk[4], n=mk[5]),
     "arc": lambda: meter_arc(sc, mk[1], mk[2], mk[3], p, 64)}[mk[0]]()
    b = g["batt"]
    batt_v(sc, b[1], b[2], b[3], w=b[4]) if b[0] == "v" else batt_h(sc, b[1], b[2], b[3], h=b[4])
    lx, ly = g["lamps"]
    if c["lamps"] == "switcher":
        lamps_switcher(sc, lx, ly, p, c["seed"], cols=g["switcher"][0], rows=g["switcher"][1])
    else:
        LAMPS[c["lamps"]](sc, lx, ly, p, c["seed"])
    rx, ry, rl, rp = g["readout"]
    readout(sc, rx, ry, p, c["seed"], lines=rl, pitch=rp)
    ox, oy, orr = g["rotor"]
    ROTORS[c["rotor"]](sc, ox, oy, p, t, r=orr) if c["rotor"] != "twin" else rotor_twin(sc, ox, oy, p, t)
    if lock:
        lock_plates(sc, p)
    else:
        keys(sc, p, c["keys"])


# ---------------- the 20 concepts ----------------
CONCEPTS = [
    dict(name="Targeting Computer", pal="targeting", layout="round", frame="notched", lamps="domes", rotor="reticle", keys="flat", meter="dots",
         note="red-on-black round scope, crosshair + range ticks, sweep reticle"),
    dict(name="Death Star Corridor", pal="deathstar", layout="standard", frame="brackets", lamps="switcher", rotor="scomp", keys="bevel",
         note="cool grey-blue, bracket corners, switcher bank of tiny lamps"),
    dict(name="Bridge Phosphor", pal="bridge", layout="wide", frame="ticks", lamps="matrix", rotor="radar", keys="strip",
         note="green phosphor, full-width scope with ruler ticks, radar sweep"),
    dict(name="ISB Terminal", pal="isb", layout="standard", frame="double", lamps="pills", rotor="square", keys="flat", meter="twin",
         note="Andor-era teal-grey with orange, double frame, square bracket rotor"),
    dict(name="Amber CRT", pal="amber", layout="wide", frame="dashed", lamps="domes", rotor="reticle", keys="ring",
         note="single-colour amber terminal, dashed frame, backlit-ring keys"),
    dict(name="Royal Guard", pal="crimson", layout="standard", frame="notched", lamps="diamonds", rotor="gear", keys="pill",
         note="crimson and white, diamond lamps, cog rotor"),
    dict(name="Stealth Ops", pal="stealth", layout="standard", frame="brackets", lamps="matrix", rotor="scomp", keys="flat", meter="dots",
         note="ultra-dark low-burn: dim greys, one muted red"),
    dict(name="TIE Cockpit", pal="tie", layout="round", frame="notched", lamps="domes", rotor="twin", keys="strip", meter="dots",
         note="blue-grey round scope, twin rotors like a TIE's paired engines"),
    dict(name="Vector Briefing", pal="vector", layout="wide", frame="notched", lamps="matrix", rotor="reticle", keys="flat",
         note="1977 briefing-room wireframe: four greys + one red"),
    dict(name="Switcher Panel", pal="standard", layout="hero", frame="notched", lamps="switcher", rotor="gear", keys="bevel",
         note="hero cog rotor, arc meter, broadcast-switcher lamp bank"),
    dict(name="Detention Block", pal="targeting", layout="mirror", frame="double", lamps="pills", rotor="square", keys="bevel",
         note="red alert block: mirrored layout, square bracket rotor"),
    dict(name="Hangar Control", pal="deathstar", layout="hero", frame="ticks", lamps="domes", rotor="radar", keys="ring",
         note="big radar sweep, ruler-tick scope, arc meter"),
    dict(name="Retro Ops", pal="retro", layout="wide", frame="brackets", lamps="diamonds", rotor="twin", keys="strip",
         note="cel-animation retro cyan + orange (Kessel-style)"),
    dict(name="Comms Relay", pal="standard", layout="wide", frame="dashed", lamps="pills", rotor="radar", keys="bevel",
         note="full-width comm scope, radar, pill lamps"),
    dict(name="Star Destroyer Command", pal="command", layout="hero", frame="double", lamps="matrix", rotor="gear", keys="flat",
         note="command grey + orange, hero cog, lamp matrix"),
    dict(name="Security Checkpoint", pal="amber", layout="round", frame="notched", lamps="pills", rotor="square", keys="bevel", meter="vseg",
         note="amber round scanner, square bracket rotor"),
    dict(name="Probe Droid", pal="crimson", layout="hero", frame="brackets", lamps="domes", rotor="reticle", keys="ring",
         note="one big red 'eye' reticle, minimal"),
    dict(name="Lambda Shuttle", pal="vector", layout="standard", frame="double", lamps="grid", rotor="gear", keys="pill",
         note="clean white-grey shuttle panel, cog rotor"),
    dict(name="Scarif Vault", pal="vault", layout="mirror", frame="ticks", lamps="matrix", rotor="scomp", keys="strip", meter="twin",
         note="data-vault teal + amber, mirrored layout"),
    dict(name="Interrogator", pal="targeting", layout="standard", frame="brackets", lamps="diamonds", rotor="radar", keys="flat", meter="dots",
         note="dark red, bracket scope, dot meter, radar"),
]

# skin 0 = the current default design, described in the same terms (its art is design/skins/default/)
DEFAULT = dict(name="Imperial Standard", pal="standard", layout="standard", frame="notched", lamps="grid",
               rotor="scomp", keys="bevel", meter="vseg", note="the original design", seed=3)


def render(c, t=1.3, mode="full"):
    global STATIC
    STATIC = mode != "full"
    try:
        sc = m.Screen()
        draw_console(sc, c, t, lock=(mode == "lock"))
        return sc
    finally:
        STATIC = False


def screen_rgb(sc):
    """device-size RGB of a Screen (no bezel), glow composited"""
    glow = sc.glow.filter(m.ImageFilter.GaussianBlur(9 * S))
    img = Image.alpha_composite(sc.img, glow).convert("RGB")
    return img.resize((m.W, m.H), Image.LANCZOS)


THUMB = (96, 117)


def thumbnail(sc):
    im = screen_rgb(sc).resize(THUMB, Image.LANCZOS)
    mask = Image.new("L", THUMB, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, THUMB[0] - 1, THUMB[1] - 1], radius=12, fill=255)
    out = Image.new("RGB", THUMB, (0, 0, 0))
    out.paste(im, (0, 0), mask)
    return out


def descriptor(i, c):
    p = PAL[c["pal"]]
    g = geom(c)
    d = dict(id=i, name=c["name"], palette=dict(p, hex="#E0A526" if c["pal"] == "standard" else p["key"]),
             frame=c["frame"], lamps=c["lamps"], rotor=c["rotor"], keys=c["keys"], seed=c["seed"],
             geom={k: list(v) for k, v in g.items()})
    d["parts"] = geom_parts(c, g)
    return d


def geom_parts(c, g):
    """the classic layouts as a part list (see sim/ui_imperial/skin.h); order = drawing order"""
    sk, mk, b = g["scope"], g["meter"], g["batt"]
    meter_g = {"vseg": mk[1:6], "dots": mk[1:5], "twin": mk[1:4], "hseg": mk[1:6], "arc": mk[1:4]}[mk[0]]
    sw = g.get("switcher", (0, 0))
    scope = ["round", 0, 0, *sk[1:4]] if sk[0] == "round" else ["scope", c["frame"], 0, *sk[1:5]]
    return [scope,
            ["meter", mk[0], 0, *meter_g],
            ["batt", b[0], 0, *b[1:5]],
            ["lamps", c["lamps"], 0, g["lamps"][0], g["lamps"][1], sw[0], sw[1]],
            ["readout", 0, 1, *g["readout"]],
            ["rotor", c["rotor"], 0, *g["rotor"]]]


def export_packs():
    """skins.json + per-skin background layers and thumbnails for the device / simulator"""
    import json
    out = os.path.join(HERE, "..", "packs")
    os.makedirs(out, exist_ok=True)
    skins = []
    allc = [DEFAULT] + CONCEPTS
    for i, c in enumerate(allc):
        c.setdefault("seed", i * 7)
        if i == 0:
            # default skin art lives in design/skins/default/; thumbnail from the mock-up
            thumbnail(m.main_screen(live=True, level=70)).save(os.path.join(out, "00_thumb.png"))
        else:
            screen_rgb(render(c, mode="static")).save(os.path.join(out, "%02d_main.png" % i))
            screen_rgb(render(c, mode="lock")).save(os.path.join(out, "%02d_lock.png" % i))
            thumbnail(render(c, t=1.0 + i * 0.37)).save(os.path.join(out, "%02d_thumb.png" % i))
        skins.append(descriptor(i, c))
    with open(os.path.join(out, "skins.json"), "w") as fh:
        json.dump(skins, fh, indent=1)
    print("wrote", len(skins), "skin packs to", os.path.normpath(out))


def main():
    shots = []
    for i, c in enumerate(CONCEPTS, 1):
        c["seed"] = i * 7
        im = render(c, t=1.0 + i * 0.37).finish()
        slug = "%02d_%s" % (i, c["name"].lower().replace(" ", "_").replace("'", ""))
        im.save(os.path.join(HERE, slug + ".png"))
        shots.append((i, c, im))
    cap_f = ImageFont.truetype(m.MONT, 26)
    note_f = ImageFont.truetype(m.MONT, 18)
    for sheet, part in enumerate((shots[:10], shots[10:]), 1):
        tw, th = part[0][2].size
        tw, th = tw // 2, th // 2
        cols = 5
        W = Image.new("RGB", (cols * (tw + 20) + 20, 2 * (th + 130) + 20), "#0D0F11")
        d = ImageDraw.Draw(W)
        for k, (i, c, im) in enumerate(part):
            x, y = 20 + (k % cols) * (tw + 20), 20 + (k // cols) * (th + 130)
            W.paste(im.resize((tw, th), Image.LANCZOS), (x, y))
            d.text((x + 4, y + th + 10), "%d  %s" % (i, c["name"]), font=cap_f, fill="#E8E8E8")
            words, line, lines = c["note"].split(), "", []
            for w in words:                                    # wrap to the tile width
                if note_f.getlength((line + " " + w).strip()) > tw - 8:
                    lines.append(line); line = w
                else:
                    line = (line + " " + w).strip()
            lines.append(line)
            for j, ln in enumerate(lines[:3]):
                d.text((x + 4, y + th + 44 + j * 22), ln, font=note_f, fill="#9AA2A8")
        W.save(os.path.join(HERE, "sheet_%d.png" % sheet))
    print("wrote", len(shots), "concepts + 2 sheets to", HERE)
    export_packs()


if __name__ == "__main__":
    main()
