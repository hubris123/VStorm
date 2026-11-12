"""Skins 22-36: free-layout Imperial concepts.

Fixed on every skin: the top bar (callsign, TX, CLIP) and the LOCK / SYS / LIVE keys
(colour + key style may change). Everything in between is free: any mix of scopes, meters,
lamps, readouts and rotors, plus new parts (bar graph, ring meter, waterfall, big gauge,
target view, tall scope, hex table, edge lamp strips).

Rules: every skin has at least one live voice display; small parts in the row right above
the keys are centred on the key centres (x 68 / 184 / 300); the secret temperature (88 F in
the mock-ups) is hidden somewhere different on each skin.

Each concept is a list of parts in the same form the device reads (skins.json "parts",
see sim/ui_imperial/skin.h): ["kind", style, flags, g...]. flags 1 = holds the temp.
Kinds starting with "_" are art only (drawn into the background, not live).
The same list draws the mock-up (full), the background layers (static / lock) and the recipe.

    python3 design/skins/concepts/make_concepts2.py
writes design/skins/concepts2/NN_name.png + sheet_3.png / sheet_4.png, and re-exports
design/skins/packs/ (skins 0-20 from make_concepts.py, then these as skins 21-35).
"""
import json
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_concepts as mc  # noqa: E402

m, S, f = mc.m, mc.S, mc.f
OUT = os.path.join(HERE, "..", "concepts2")
PACKS = os.path.join(HERE, "..", "packs")
KEYX = (68, 184, 300)
PIL2PX = 1.27            # mock-up font size -> device px (PIL 33 = the 42 px callsign)

PAL = dict(mc.PAL)
PAL.update({
    "snow":     dict(frame="#9AB4C8", grid="#16212B", trace="#BFE3FF", accent="#5FA8E8", text="#E4F0FA",
                     tick="#6F8EA6", lamps=["#E4F0FA", "#5FA8E8", "#C4262E", "#2E4A62"], key="#BFE3FF"),
    "desert":   dict(frame="#9A7A4A", grid="#241C10", trace="#E8C27A", accent="#D2691E", text="#F0DCB4",
                     tick="#8A6A3A", lamps=["#E8C27A", "#D2691E", "#7A5A2A", "#C4262E"], key="#E8C27A"),
    "forest":   dict(frame="#5E7A52", grid="#122014", trace="#9CD38A", accent="#D8A23A", text="#CFE3C4",
                     tick="#5A7A40", lamps=["#9CD38A", "#3E5E36", "#D8A23A", "#C4262E"], key="#9CD38A"),
    "darkops":  dict(frame="#2E4A48", grid="#0A1514", trace="#3FD6B8", accent="#C4262E", text="#9FCFC6",
                     tick="#2E6A60", lamps=["#3FD6B8", "#1E3A38", "#C4262E", "#5A6A68"], key="#3FD6B8"),
    "lava":     dict(frame="#8A3A1A", grid="#240C06", trace="#FF8A3A", accent="#E0402E", text="#F8C8A8",
                     tick="#9A4A20", lamps=["#FF8A3A", "#E0402E", "#F0C040", "#5A1A0A"], key="#FF8A3A"),
    "night":    dict(frame="#3A4A6A", grid="#0C1220", trace="#7A9AE0", accent="#9A3AE0", text="#B8C4E8",
                     tick="#4A5A88", lamps=["#7A9AE0", "#2A3A5A", "#9A3AE0", "#C8D0F0"], key="#7A9AE0"),
    "olive":    dict(frame="#6A6A3A", grid="#18180C", trace="#C8C870", accent="#E0A526", text="#E0E0B8",
                     tick="#7A7A40", lamps=["#C8C870", "#6A6A3A", "#E0A526", "#C4262E"], key="#C8C870"),
    "steel":    dict(frame="#7A8A9A", grid="#141A20", trace="#A8C8E8", accent="#E8E8E8", text="#D8E0E8",
                     tick="#6A7A8A", lamps=["#A8C8E8", "#4A5A6A", "#E8E8E8", "#C4262E"], key="#A8C8E8"),
    "ember":    dict(frame="#7A4A2A", grid="#1E1008", trace="#F0A040", accent="#C4262E", text="#F0D0B0",
                     tick="#8A5A30", lamps=["#F0A040", "#C4262E", "#7A4A2A", "#F0D060"], key="#F0A040"),
})


def live():
    return not mc.STATIC


def mix(a, b, t):
    return m.mix(a, b, t)


def level_series(n, seed):
    rnd = random.Random(seed)
    return [max(5, min(98, 30 + 55 * abs(math.sin(i / 2.3 + seed)) + rnd.uniform(-15, 15))) for i in range(n)]


# ---------------- new parts (static bits always, live bits only in the mock-up) ----------------
def bargraph(sc, x0, y0, x1, y1, p, n=16, seed=1):
    """voice bar graph: n segmented bars of recent level (device: a canvas, same geometry)"""
    sc.rect(x0, y0, x1, y1, fill="#06080A")
    bw = (x1 - x0 - 8) / n
    seg = 5
    nseg = int((y1 - y0 - 8) / seg)
    vals = level_series(n, seed) if live() else [0] * n
    for i, v in enumerate(vals):
        bx = x0 + 4 + i * bw
        lit = int(nseg * v / 100)
        for k in range(nseg):
            yy = y1 - 4 - (k + 1) * seg
            col = (p["accent"] if k > nseg * 0.78 else p["trace"]) if k < lit else "#0E1215"
            sc.rect(bx + 1, yy + 1, bx + bw - 2, yy + seg - 1, fill=col)
    m.notched_frame(sc, x0 - 3, y0 - 3, x1 + 3, y1 + 3, p["frame"])


def ring_meter(sc, cx, cy, r, p, level, n=28, w=6, gap_deg=4):
    """segmented ring around a rotor: lit clockwise from the lower left by voice level"""
    step = 300 / n
    for i in range(n):
        a0 = 120 + i * step
        lit = live() and i < level * n / 100
        col = (p["accent"] if i > n * 0.8 else p["trace"]) if lit else "#121619"
        sc.arc(cx, cy, r, a0, a0 + step - gap_deg, col, w)


def waterfall(sc, x0, y0, x1, y1, p, t, rows=9):
    """scrolling voice waterfall: stacked traces, newest bright at the bottom"""
    sc.rect(x0, y0, x1, y1, fill="#06080A")
    for k in range(1, 6):
        sc.line([(x0 + (x1 - x0) * k / 6, y0), (x0 + (x1 - x0) * k / 6, y1)], p["grid"], 1)
    m.notched_frame(sc, x0 - 3, y0 - 3, x1 + 3, y1 + 3, p["frame"])
    if not live():
        return
    pitch = (y1 - y0 - 16) / rows
    for r_ in range(rows):
        base = y0 + 14 + r_ * pitch
        wave = m.voice_wave(50, 7 + r_, t + r_ * 3)
        amp = 0.25 + 0.75 * (r_ / (rows - 1))
        col = mix("#000000", p["trace"], 0.25 + 0.75 * (r_ / (rows - 1)))
        pts = [(x0 + 3 + (x1 - x0 - 6) * i / 49, base - (v / 100) * pitch * 1.6 * amp) for i, v in enumerate(wave)]
        sc.line(pts, col, 2 if r_ == rows - 1 else 1)


def big_gauge(sc, cx, cy, r, p, level, plate=False):
    """large semicircle gauge; the plate under the pivot can hold the temp"""
    sc.arc(cx, cy, r, 180, 360, "#151A1F", 14)
    sc.arc(cx, cy, r - 18, 180, 360, mix(p["frame"], "#000000", 0.4), 2)
    for i in range(11):
        a = math.radians(180 + i * 18)
        ca, sa = math.cos(a), math.sin(a)
        sc.line([(cx + (r + 4) * ca, cy + (r + 4) * sa), (cx + (r + 12) * ca, cy + (r + 12) * sa)], p["frame"], 2)
        if i % 2 == 0:
            sc.text(cx + (r - 30) * ca, cy + (r - 30) * sa, str(i * 10), f(m.MONT, 11), p["tick"], "mm")
    if plate:
        sc.d.rounded_rectangle(sc.b(cx - 22, cy + 10, cx + 22, cy + 28), radius=3 * S, fill="#0C0F12",
                               outline=mix(p["frame"], "#000000", 0.3), width=S)
    if not live():
        return
    sc.arc(cx, cy, r, 180, 180 + 180 * level / 100, p["trace"], 14)
    a = math.radians(180 + 180 * level / 100)
    sc.line([(cx, cy), (cx + (r - 22) * math.cos(a), cy + (r - 22) * math.sin(a))], p["accent"], 3)
    sc.d.ellipse(sc.b(cx - 9, cy - 9, cx + 9, cy + 9), fill=p["frame"])
    if plate:
        sc.text(cx, cy + 19, "88", f(m.AURENG, 15), p["tick"], "mm")


def hex_table(sc, x0, y0, cols, rows, cw, rh, p, seed, temp_cell=None):
    """columns of hex readings (all live); temp_cell shows the temp in hex (88 F -> 58)"""
    if not live():
        return
    rnd = random.Random(seed)
    for r_ in range(rows):
        for c in range(cols):
            txt = "%02X" % rnd.randrange(256)
            if temp_cell == r_ * cols + c:
                txt = "58"
            col = p["tick"] if (r_ + c) % 3 else mix(p["tick"], "#FFFFFF", 0.25)
            sc.text(x0 + c * cw, y0 + r_ * rh, txt, f(m.MONT, 14), col, "lm")


def target_view(sc, b, p, t):
    """viewport with crosshair and range ticks (art); trace + locking brackets (live)"""
    x0, y0, x1, y1 = b
    sc.rect(x0, y0, x1, y1, fill="#06080A")
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    sc.line([(x0, cy), (x1, cy)], p["grid"], 1)
    sc.line([(cx, y0), (cx, y1)], p["grid"], 1)
    for k in range(-6, 7):
        sc.line([(cx + k * 20, cy - 3), (cx + k * 20, cy + 3)], p["grid"], 1)
    mc.FRAMES["brackets"](sc, b, p)
    if not live():
        return
    wave = m.voice_wave(60, 11, t)
    pts = [(x0 + 3 + (x1 - x0 - 6) * i / 59, y1 - 6 - (y1 - y0 - 12) * v / 100 * 0.6) for i, v in enumerate(wave)]
    sc.line(pts, mix("#000000", p["trace"], 0.7), 2)
    w2, h2 = 46, 30                                          # brackets close in while talking
    for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
        ex, ey = cx + sx * w2, cy + sy * h2
        sc.line([(ex - sx * 14, ey), (ex, ey), (ex, ey - sy * 12)], p["accent"], 3)


def vscope(sc, x0, y0, x1, y1, p, t, frame="notched"):
    """tall scope: the trace runs top to bottom, level sideways"""
    sc.rect(x0, y0, x1, y1, fill="#06080A")
    for k in range(1, 3):
        sc.line([(x0 + (x1 - x0) * k / 3, y0), (x0 + (x1 - x0) * k / 3, y1)], p["grid"], 1)
    for k in range(1, 8):
        sc.line([(x0, y0 + (y1 - y0) * k / 8), (x1, y0 + (y1 - y0) * k / 8)], p["grid"], 1)
    mc.FRAMES[frame](sc, (x0, y0, x1, y1), p)
    if not live():
        return
    wave = m.voice_wave(60, 13, t)
    pts = [(x0 + 4 + (x1 - x0 - 8) * v / 100, y0 + 3 + (y1 - y0 - 6) * i / 59) for i, v in enumerate(wave)]
    sc.line(pts, p["trace"], 2)


def edge_strip(sc, x, y0, y1, p, seed, w=10, h=7, gap=5):
    rnd = random.Random(seed)
    y = y0
    while y + h <= y1:
        s_ = rnd.randrange(5)
        col = (p["lamps"][s_ % 4] if s_ < 4 else "#151A1F") if live() else "#151A1F"
        sc.rect(x, y, x + w, y + h, fill=col)
        y += h + gap


def temp_tag(sc, x, y, anchor, px, prefix, p):
    if not live():
        return
    size = round(px / PIL2PX)
    text = ("", "RNG ", "CH ")[prefix] + "88"
    font = f(m.MONT, size) if prefix else f(m.AURENG, size)
    sc.text(x, y, text, font, p["tick"], ("mm", "rm", "lm")[anchor])


def readout2(sc, cx, cy, p, seed, lines, pitch, temp):
    """Aurebesh readout; line 0 holds the temp only when this skin keeps it there"""
    if not live():
        return
    if temp:
        mc.readout(sc, cx, cy, p, seed, lines=lines, pitch=pitch)
        return
    rnd = random.Random(seed)
    for y in mc.readout_rows(cy, lines, pitch):
        sc.text(cx, y, rnd.choice(mc.WORDS), f(m.AUREB, 13), p["tick"], "mm")


def lamps(sc, kind, g, p, seed):
    cx, cy = g[0], g[1]
    cols, rows, r = (list(g[2:5]) + [0, 0, 0])[:3]
    if kind == "domes":
        mc.lamps_domes(sc, cx, cy, p, seed, cols=cols or 3, rows=rows or 3, r=r or 11)
    elif kind == "switcher":
        mc.lamps_switcher(sc, cx, cy, p, seed, cols=cols or 8, rows=rows or 5)
    else:
        mc.LAMPS[kind](sc, cx, cy, p, seed)


def meter(sc, kind, g, p):
    if kind == "vseg":
        mc.meter_vseg(sc, g[0], g[1], g[2], p, 66, w=g[3], n=g[4])
    elif kind == "dots":
        mc.meter_dots(sc, g[0], g[1], g[2], p, 66, n=g[3])
    elif kind == "twin":
        mc.meter_twin(sc, g[0], g[1], g[2], p, 70)
    elif kind == "hseg":
        mc.meter_hseg(sc, g[0], g[1], g[2], p, 60, h=g[3], n=g[4])
    else:
        mc.meter_arc(sc, g[0], g[1], g[2], p, 60)


def draw_part(sc, p, pt, t, seed):
    kind, style, flags, g = pt[0], pt[1], pt[2], list(pt[3:])
    if kind == "scope":
        mc.scope_rect(sc, g[:4], p, style, t + seed % 7 * 3, tilt=(12, -6) if seed % 2 else (-14, 8))
    elif kind == "round":
        mc.scope_round(sc, g[0], g[1], g[2], p, t)
    elif kind == "vscope":
        vscope(sc, *g[:4], p, t, frame=style)
    elif kind == "target":
        target_view(sc, g[:4], p, t)
    elif kind == "waterfall":
        waterfall(sc, *g[:4], p, t, rows=g[4])
    elif kind == "meter":
        meter(sc, style, g, p)
    elif kind == "batt":
        (mc.batt_v(sc, g[0], g[1], g[2], w=g[3]) if style == "v" else mc.batt_h(sc, g[0], g[1], g[2], h=g[3]))
    elif kind == "lamps":
        lamps(sc, style, g, p, seed)
    elif kind == "edge":
        edge_strip(sc, g[0], g[1], g[2], p, seed, w=g[3])
    elif kind == "readout":
        readout2(sc, g[0], g[1], p, seed, g[2], g[3], flags & 1)
    elif kind == "rotor":
        (mc.rotor_twin(sc, g[0], g[1], p, t) if style == "twin" else mc.ROTORS[style](sc, g[0], g[1], p, t, r=g[2]))
    elif kind == "bars":
        bargraph(sc, *g[:4], p, n=g[4], seed=seed)
    elif kind == "ring":
        ring_meter(sc, g[0], g[1], g[2], p, 68, n=g[3], w=g[4])
    elif kind == "gauge":
        big_gauge(sc, g[0], g[1], g[2], p, 64, plate=bool(flags & 1))
    elif kind == "hex":
        hex_table(sc, *g[:6], p, seed, temp_cell=style if flags & 1 else None)
    elif kind == "temp":
        temp_tag(sc, g[0], g[1], g[2], g[3], style, p)
    elif kind == "_frame":                                   # art only: a notched frame
        m.notched_frame(sc, *g[:4], p["frame"])
    elif kind == "_text":                                    # art only: small fixed labels
        x0, y, dx, size, *labels = g
        for i, s_ in enumerate(labels):
            sc.text(x0 + i * dx, y, s_, f(m.MONT, size), p["tick"], "mm")
    else:
        raise ValueError(kind)


def T(px):
    """font px for a temp tag drawn at mock-up size px"""
    return round(px * PIL2PX)


# ---------------- the 15 concepts ----------------
CONCEPTS2 = [
    dict(name="Snow Patrol", pal="snow", keys="ring", note="ice white + blue; big gauge, temp on the pivot plate", parts=[
        ["gauge", 0, 1, 184, 176, 112],
        ["batt", "h", 0, 100, 268, 212, 4],
        ["lamps", "domes", 0, 68, 262, 3, 2, 11],
        ["readout", 0, 0, 184, 262, 3, 24],
        ["rotor", "scomp", 0, 300, 262, 38]]),
    dict(name="Desert Outpost", pal="desert", keys="strip", note="sand + rust; twin scopes, full-width meter", parts=[
        ["scope", "double", 0, 18, 62, 178, 158],
        ["scope", "double", 0, 190, 62, 350, 158],
        ["meter", "hseg", 0, 18, 350, 172, 10, 24],
        ["batt", "h", 0, 18, 350, 188, 5],
        ["lamps", "pills", 0, 68, 252],
        ["readout", 0, 1, 184, 252, 4, 28],
        ["rotor", "gear", 0, 300, 252, 42]]),
    dict(name="Forest Scout", pal="forest", keys="flat", note="tall scope + bar graph; temp as the graph tag", parts=[
        ["vscope", "notched", 0, 18, 62, 110, 298],
        ["bars", 0, 0, 132, 62, 350, 176, 14],
        ["temp", 0, 1, 346, 185, 1, T(11)],
        ["batt", "v", 0, 119, 62, 298, 4],
        ["lamps", "matrix", 0, 184, 250],
        ["rotor", "radar", 0, 300, 250, 44]]),
    dict(name="Dark Ops", pal="darkops", keys="flat", note="teal on black waterfall; temp as the channel tag", parts=[
        ["waterfall", 0, 0, 18, 62, 350, 196, 9],
        ["temp", 0, 1, 24, 69, 2, T(11)],
        ["batt", "h", 0, 18, 350, 206, 4],
        ["lamps", "switcher", 0, 68, 262, 6, 4],
        ["readout", 0, 0, 184, 262, 3, 24],
        ["rotor", "twin", 0, 300, 258, 0]]),
    dict(name="Lava Forge", pal="lava", keys="bevel", note="voice ring round a big cog, edge lamp strips", parts=[
        ["edge", 0, 0, 18, 60, 206, 10],
        ["edge", 0, 0, 340, 60, 206, 10],
        ["ring", 0, 0, 184, 148, 84, 28, 8],
        ["rotor", "gear", 0, 184, 148, 66],
        ["batt", "v", 0, 36, 60, 206, 4],
        ["lamps", "diamonds", 0, 68, 268],
        ["readout", 0, 1, 184, 268, 3, 24],
        ["rotor", "reticle", 0, 300, 268, 36]]),
    dict(name="Target Lock", pal="targeting", keys="flat", note="locking target view; temp as the range reading", parts=[
        ["target", 0, 0, 18, 62, 350, 192],
        ["temp", 1, 1, 24, 184, 2, T(11)],
        ["batt", "h", 0, 18, 350, 202, 4],
        ["lamps", "domes", 0, 68, 262, 3, 2, 11],
        ["meter", "arc", 0, 184, 286, 40],
        ["rotor", "radar", 0, 300, 262, 40]]),
    dict(name="Archive", pal="vault", keys="strip", note="hex table; temp hidden in hex (58 = 88)", parts=[
        ["scope", "ticks", 0, 18, 62, 168, 176],
        ["_frame", 0, 0, 176, 59, 352, 179],
        ["hex", 14, 1, 186, 72, 4, 6, 40, 19],
        ["batt", "h", 0, 18, 350, 193, 4],
        ["lamps", "matrix", 0, 68, 256],
        ["bars", 0, 0, 140, 218, 228, 296, 8],
        ["rotor", "scomp", 0, 300, 256, 42]]),
    dict(name="Sentinel", pal="standard", keys="bevel", note="big Scomp with voice ring, temp under the hub", parts=[
        ["edge", 0, 0, 20, 66, 226, 8],
        ["edge", 0, 0, 340, 66, 226, 8],
        ["ring", 0, 0, 184, 146, 86, 32, 7],
        ["rotor", "scomp", 0, 184, 146, 70],
        ["temp", 0, 1, 184, 178, 0, T(12)],
        ["batt", "v", 0, 36, 66, 226, 4],
        ["lamps", "pills", 0, 68, 268],
        ["readout", 0, 0, 184, 276, 2, 24],
        ["lamps", "pills", 0, 300, 268]]),
    dict(name="Signal Tower", pal="retro", keys="strip", note="wide spectrum bars; temp as the last channel", parts=[
        ["bars", 0, 0, 18, 62, 350, 172, 22],
        ["_text", 0, 0, 30, 183, 68, 10, "01", "05", "09", "13", "17"],
        ["temp", 0, 1, 344, 183, 1, T(11)],
        ["batt", "h", 0, 18, 350, 194, 4],
        ["lamps", "grid", 0, 68, 262],
        ["readout", 0, 0, 184, 258, 4, 29],
        ["rotor", "radar", 0, 300, 262, 44]]),
    dict(name="Night Watch", pal="night", keys="ring", note="tall centre scope, lamp bank, dot meter", parts=[
        ["vscope", "brackets", 0, 140, 60, 228, 300],
        ["temp", 0, 1, 184, 66, 0, T(11)],
        ["lamps", "switcher", 0, 68, 180, 6, 8],
        ["meter", "dots", 0, 300, 60, 300, 16],
        ["batt", "v", 0, 330, 60, 300, 6],
        ["rotor", "square", 0, 68, 278, 26]]),
    dict(name="Command Deck", pal="command", keys="bevel", note="stacked twin scopes; temp as a channel tag", parts=[
        ["scope", "notched", 0, 18, 62, 288, 116],
        ["scope", "notched", 0, 18, 126, 288, 180],
        ["temp", 2, 1, 282, 135, 1, T(10)],
        ["meter", "vseg", 0, 306, 49, 178, 36, 12],
        ["batt", "v", 0, 348, 49, 178, 6],
        ["lamps", "grid", 0, 68, 250],
        ["readout", 0, 0, 184, 246, 4, 29],
        ["rotor", "gear", 0, 300, 250, 44]]),
    dict(name="Interceptor Wing", pal="tie", keys="pill", note="round scope flanked by bar graphs", parts=[
        ["round", 0, 0, 184, 136, 74],
        ["bars", 0, 0, 18, 66, 90, 206, 4],
        ["bars", 0, 0, 278, 66, 350, 206, 4],
        ["batt", "h", 0, 112, 256, 224, 4],
        ["lamps", "diamonds", 0, 68, 268],
        ["readout", 0, 1, 184, 266, 3, 24],
        ["rotor", "square", 0, 300, 268, 36]]),
    dict(name="Bunker", pal="olive", keys="bevel", note="waterfall + big gauge; temp in hex", parts=[
        ["waterfall", 0, 0, 18, 62, 176, 196, 7],
        ["gauge", 0, 0, 268, 168, 78],
        ["batt", "h", 0, 192, 344, 188, 4],
        ["lamps", "switcher", 0, 68, 262, 6, 4],
        ["hex", 1, 1, 158, 244, 2, 3, 38, 18],
        ["rotor", "scomp", 0, 300, 262, 40]]),
    dict(name="Cold Steel", pal="steel", keys="ring", note="wide scope, voice-ringed rotor, temp in its hub", parts=[
        ["edge", 0, 0, 18, 64, 168, 8],
        ["scope", "double", 0, 36, 62, 350, 168],
        ["batt", "h", 0, 36, 350, 180, 4],
        ["lamps", "domes", 0, 68, 258, 3, 3, 10],
        ["readout", 0, 0, 184, 258, 3, 26],
        ["ring", 0, 0, 300, 258, 46, 22, 6],
        ["rotor", "scomp", 0, 300, 258, 36],
        ["temp", 0, 1, 300, 276, 0, T(10)]]),
    dict(name="Ember", pal="ember", keys="pill", note="big gauge + tall scope; temp on the gauge plate", parts=[
        ["gauge", 0, 1, 120, 168, 92],
        ["vscope", "double", 0, 248, 60, 350, 214],
        ["batt", "h", 0, 28, 212, 208, 4],
        ["lamps", "pills", 0, 68, 262],
        ["readout", 0, 0, 184, 262, 3, 24],
        ["rotor", "gear", 0, 300, 262, 40]]),
]
FIRST_ID = 21          # skins 0-20 are make_concepts.py's


def render(c, t=1.3, mode="full", seed=0):
    p = PAL[c["pal"]]
    mc.STATIC = mode != "full"
    try:
        sc = m.Screen()
        mc.top_bar(sc, p, lock=(mode == "lock"))
        for i, pt in enumerate(c["parts"]):
            draw_part(sc, p, pt, t, seed + i)
        if mode == "lock":
            mc.lock_plates(sc, p)
        else:
            mc.keys(sc, p, c["keys"])
        return sc
    finally:
        mc.STATIC = False


def export():
    mc.export_packs()                                        # skins 0-20 (rewrites skins.json)
    path = os.path.join(PACKS, "skins.json")
    skins = [s for s in json.load(open(path)) if s["id"] < FIRST_ID]
    for k, c in enumerate(CONCEPTS2):
        i, seed = FIRST_ID + k, (FIRST_ID + k) * 7
        mc.screen_rgb(render(c, mode="static", seed=seed)).save(os.path.join(PACKS, "%02d_main.png" % i))
        mc.screen_rgb(render(c, mode="lock", seed=seed)).save(os.path.join(PACKS, "%02d_lock.png" % i))
        mc.thumbnail(render(c, t=1.0 + i * 0.37, seed=seed)).save(os.path.join(PACKS, "%02d_thumb.png" % i))
        p = PAL[c["pal"]]
        parts = [list(pt) for pt in c["parts"] if not pt[0].startswith("_")]
        skins.append(dict(id=i, name=c["name"], palette=dict(p, hex=p["key"]), keys=c["keys"], seed=seed, parts=parts))
    with open(path, "w") as fh:
        json.dump(skins, fh, indent=1)
    print("wrote", len(skins), "skin packs to", os.path.normpath(PACKS))


def main():
    os.makedirs(OUT, exist_ok=True)
    shots = []
    for k, c in enumerate(CONCEPTS2):
        num = 22 + k
        im = render(c, 1.0 + num * 0.31, seed=(FIRST_ID + k) * 7).finish()
        im.save(os.path.join(OUT, "%02d_%s.png" % (num, c["name"].lower().replace(" ", "_"))))
        shots.append((num, c["name"], c["note"], im))
    cap_f = ImageFont.truetype(m.MONT, 26)
    note_f = ImageFont.truetype(m.MONT, 18)
    for sheet, part in ((3, shots[:8]), (4, shots[8:])):
        tw, th = part[0][3].size
        tw, th = tw // 2, th // 2
        cols = 4
        rows = (len(part) + cols - 1) // cols
        W = Image.new("RGB", (cols * (tw + 20) + 20, rows * (th + 130) + 20), "#0D0F11")
        d = ImageDraw.Draw(W)
        for k, (num, name, note, im) in enumerate(part):
            x, y = 20 + (k % cols) * (tw + 20), 20 + (k // cols) * (th + 130)
            W.paste(im.resize((tw, th), Image.LANCZOS), (x, y))
            d.text((x + 4, y + th + 10), "%d  %s" % (num, name), font=cap_f, fill="#E8E8E8")
            words, line, lines = note.split(), "", []
            for w in words:
                if note_f.getlength((line + " " + w).strip()) > tw - 8:
                    lines.append(line); line = w
                else:
                    line = (line + " " + w).strip()
            lines.append(line)
            for j, ln in enumerate(lines[:3]):
                d.text((x + 4, y + th + 44 + j * 22), ln, font=note_f, fill="#9AA2A8")
        W.save(os.path.join(OUT, "sheet_%d.png" % sheet))
    print("wrote", len(shots), "concepts + 2 sheets to", os.path.normpath(OUT))
    export()


if __name__ == "__main__":
    main()
