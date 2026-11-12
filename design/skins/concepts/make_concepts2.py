"""Skins 22-36: free-layout Imperial concepts.

Fixed on every skin: the top bar (callsign, TX, CLIP) and the LOCK / SYS / LIVE keys
(colour + key style may change). Everything in between is free: any mix of scopes, meters,
lamps, readouts and rotors, plus new parts (bar graph, ring meter, waterfall, dual scope,
big gauge, hex table, target view, tall scope, edge lamp strips).

Rules: every skin has at least one live voice display; small parts in the row right above
the keys are centred on the key centres (x 68 / 184 / 300); the secret temperature (88 F
here) is hidden somewhere different on each skin.

    python3 design/skins/concepts/make_concepts2.py
writes design/skins/concepts2/NN_name.png and sheet_3.png / sheet_4.png
"""
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
KEYX = (68, 184, 300)

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


def mix(a, b, t):
    return m.mix(a, b, t)


def level_series(n, seed):
    rnd = random.Random(seed)
    return [max(5, min(98, 30 + 55 * abs(math.sin(i / 2.3 + seed)) + rnd.uniform(-15, 15))) for i in range(n)]


# ---------------- new parts ----------------
def bargraph(sc, x0, y0, x1, y1, p, n=16, seed=1, label=None):
    """voice bar graph: n vertical segmented bars (recent level history)"""
    sc.rect(x0, y0, x1, y1, fill="#06080A")
    bw = (x1 - x0 - 8) / n
    seg = 5
    vals = level_series(n, seed)
    for i, v in enumerate(vals):
        bx = x0 + 4 + i * bw
        hgt = (y1 - y0 - 8)
        nseg = int(hgt / seg)
        lit = int(nseg * v / 100)
        for k in range(nseg):
            yy = y1 - 4 - (k + 1) * seg
            col = (p["accent"] if k > nseg * 0.78 else p["trace"]) if k < lit else "#0E1215"
            sc.rect(bx + 1, yy + 1, bx + bw - 2, yy + seg - 1, fill=col)
    m.notched_frame(sc, x0 - 3, y0 - 3, x1 + 3, y1 + 3, p["frame"])
    if label:
        sc.text(x1 - 4, y1 + 9, label, f(m.MONT, 11), p["tick"], "rm")


def ring_meter(sc, cx, cy, r, p, level, n=28, w=6, gap_deg=4):
    """segmented ring around a rotor: lit clockwise from the bottom by voice level"""
    step = 300 / n
    for i in range(n):
        a0 = 120 + i * step
        lit = i < level * n / 100
        col = (p["accent"] if i > n * 0.8 else p["trace"]) if lit else "#121619"
        sc.arc(cx, cy, r, a0, a0 + step - gap_deg, col, w)


def waterfall(sc, x0, y0, x1, y1, p, t, rows=9, label=None):
    """scrolling voice waterfall: stacked traces, newest bright at the bottom"""
    sc.rect(x0, y0, x1, y1, fill="#06080A")
    for k in range(1, 6):
        sc.line([(x0 + (x1 - x0) * k / 6, y0), (x0 + (x1 - x0) * k / 6, y1)], p["grid"], 1)
    pitch = (y1 - y0 - 16) / rows
    for r_ in range(rows):
        base = y0 + 14 + r_ * pitch
        wave = m.voice_wave(50, 7 + r_, t + r_ * 3)
        amp = 0.25 + 0.75 * (r_ / (rows - 1))
        col = mix("#000000", p["trace"], 0.25 + 0.75 * (r_ / (rows - 1)))
        pts = [(x0 + 3 + (x1 - x0 - 6) * i / 49, base - (v / 100) * pitch * 1.6 * amp) for i, v in enumerate(wave)]
        sc.line(pts, col, 2 if r_ == rows - 1 else 1)
    m.notched_frame(sc, x0 - 3, y0 - 3, x1 + 3, y1 + 3, p["frame"])
    if label:
        sc.text(x0 + 6, y0 + 7, label, f(m.MONT, 11), p["tick"], "lm")


def big_gauge(sc, cx, cy, r, p, level, scale_temp=False, plate=None):
    """large semicircle gauge; optional temp hidden as the pivot plate number"""
    sc.arc(cx, cy, r, 180, 360, "#151A1F", 14)
    sc.arc(cx, cy, r, 180, 180 + 180 * level / 100, p["trace"], 14)
    sc.arc(cx, cy, r - 18, 180, 360, mix(p["frame"], "#000000", 0.4), 2)
    for i in range(11):
        a = math.radians(180 + i * 18)
        ca, sa = math.cos(a), math.sin(a)
        sc.line([(cx + (r + 4) * ca, cy + (r + 4) * sa), (cx + (r + 12) * ca, cy + (r + 12) * sa)], p["frame"], 2)
        if i % 2 == 0:
            sc.text(cx + (r - 30) * ca, cy + (r - 30) * sa, str(i * 10), f(m.MONT, 11), p["tick"], "mm")
    a = math.radians(180 + 180 * level / 100)
    sc.line([(cx, cy), (cx + (r - 22) * math.cos(a), cy + (r - 22) * math.sin(a))], p["accent"], 3)
    sc.d.ellipse(sc.b(cx - 9, cy - 9, cx + 9, cy + 9), fill=p["frame"])
    if plate:
        sc.d.rounded_rectangle(sc.b(cx - 22, cy + 10, cx + 22, cy + 28), radius=3 * S, fill="#0C0F12",
                               outline=mix(p["frame"], "#000000", 0.3), width=S)
        sc.text(cx, cy + 19, plate, f(m.AURENG, 15), p["tick"], "mm")


def hex_table(sc, x0, y0, cols, rows, p, seed, temp_cell=None, cw=40, rh=20):
    """columns of hex readings; temp_cell=(col,row) shows the temp in hex (88 F -> 58)"""
    rnd = random.Random(seed)
    for r_ in range(rows):
        for c in range(cols):
            txt = "%02X" % rnd.randrange(256)
            if temp_cell == (c, r_):
                txt = "58"                                     # 0x58 = 88: the secret temp
            col = p["tick"] if (r_ + c) % 3 else mix(p["tick"], "#FFFFFF", 0.25)
            sc.text(x0 + c * cw, y0 + r_ * rh, txt, f(m.MONT, 14), col, "lm")


def target_view(sc, b, p, t, side=None):
    """viewport with locking brackets, crosshair and range ticks; trace inside"""
    x0, y0, x1, y1 = b
    sc.rect(x0, y0, x1, y1, fill="#06080A")
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    sc.line([(x0, cy), (x1, cy)], p["grid"], 1)
    sc.line([(cx, y0), (cx, y1)], p["grid"], 1)
    for k in range(-6, 7):
        sc.line([(cx + k * 20, cy - 3), (cx + k * 20, cy + 3)], p["grid"], 1)
    wave = m.voice_wave(60, 11, t)
    pts = [(x0 + 3 + (x1 - x0 - 6) * i / 59, y1 - 6 - (y1 - y0 - 12) * v / 100 * 0.6) for i, v in enumerate(wave)]
    sc.line(pts, mix("#000000", p["trace"], 0.7), 2)
    w2, h2 = 46, 30                                          # locking brackets (shrink when keyed)
    for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
        ex, ey = cx + sx * w2, cy + sy * h2
        sc.line([(ex - sx * 14, ey), (ex, ey), (ex, ey - sy * 12)], p["accent"], 3)
    frame = mc.FRAMES["brackets"]
    frame(sc, b, p)
    if side:
        sc.text(x0 + 6, y1 - 8, side, f(m.MONT, 11), p["tick"], "lm")


def vscope(sc, x0, y0, x1, y1, p, t, frame="notched", label=None):
    """tall scope: the trace runs top to bottom, level sideways"""
    sc.rect(x0, y0, x1, y1, fill="#06080A")
    for k in range(1, 3):
        sc.line([(x0 + (x1 - x0) * k / 3, y0), (x0 + (x1 - x0) * k / 3, y1)], p["grid"], 1)
    for k in range(1, 8):
        sc.line([(x0, y0 + (y1 - y0) * k / 8), (x1, y0 + (y1 - y0) * k / 8)], p["grid"], 1)
    wave = m.voice_wave(60, 13, t)
    pts = [(x0 + 4 + (x1 - x0 - 8) * v / 100, y0 + 3 + (y1 - y0 - 6) * i / 59) for i, v in enumerate(wave)]
    sc.line(pts, p["trace"], 2)
    mc.FRAMES[frame](sc, (x0, y0, x1, y1), p)
    if label:
        sc.text((x0 + x1) / 2, y1 + 11, label, f(m.MONT, 11), p["tick"], "mm")


def edge_strip(sc, x, y0, y1, p, seed, w=10, h=7, gap=5):
    rnd = random.Random(seed)
    y = y0
    while y + h <= y1:
        s_ = rnd.randrange(5)
        col = p["lamps"][s_ % 4] if s_ < 4 else "#151A1F"
        sc.rect(x, y, x + w, y + h, fill=col)
        y += h + gap


def small_temp(sc, x, y, text, p, anchor="mm", size=13, font=None):
    sc.text(x, y, text, font or f(m.AURENG, size), p["tick"], anchor)


# ---------------- the 15 concepts ----------------
def c22(sc, p, t):          # Snow Patrol: big gauge hero
    big_gauge(sc, 184, 188, 112, p, 64, plate="88")
    mc.batt_h(sc, 72, 296, 228, h=4)
    mc.lamps_domes(sc, 68, 262, p, 22, cols=3, rows=2)
    mc.readout(sc, 184, 262, p, 22, lines=3, pitch=24)
    mc.rotor_scomp(sc, 300, 262, p, t, r=38)


def c23(sc, p, t):          # Desert Outpost: dual scopes + full-width segment meter
    mc.scope_rect(sc, (18, 62, 178, 158), p, "double", t)
    mc.scope_rect(sc, (190, 62, 350, 158), p, "double", t + 9, tilt=(-14, 8))
    mc.meter_hseg(sc, 18, 350, 172, p, 60)
    mc.batt_h(sc, 18, 350, 188)
    mc.lamps_pills(sc, 68, 252, p, 23)
    mc.readout(sc, 184, 252, p, 23, lines=4, pitch=28)
    mc.rotor_gear(sc, 300, 252, p, t, r=42)


def c24(sc, p, t):          # Forest Scout: tall scope left, bar graph right
    vscope(sc, 18, 62, 110, 298, p, t, label=None)
    bargraph(sc, 132, 62, 350, 176, p, n=14, seed=24, label="88")      # temp as the graph's scale tag
    mc.batt_v(sc, 122, 62, 298, w=4)
    mc.lamps_matrix(sc, 184, 250, p, 24)
    mc.rotor_radar(sc, 300, 250, p, t, r=44)


def c25(sc, p, t):          # Dark Ops: waterfall
    waterfall(sc, 18, 62, 350, 196, p, t, label="88")                   # temp as the corner channel tag
    mc.batt_h(sc, 18, 350, 206, h=4)
    mc.lamps_switcher(sc, 68, 262, p, 25, cols=6, rows=4)
    mc.readout(sc, 184, 262, p, 25, lines=3, pitch=24)
    mc.rotor_twin(sc, 300, 258, p, t)


def c26(sc, p, t):          # Lava Forge: ring meter round a big rotor, edge lamp strips
    edge_strip(sc, 18, 64, 230, p, 26)
    edge_strip(sc, 340, 64, 230, p, 27)
    ring_meter(sc, 184, 148, 84, p, 66, w=8)
    mc.rotor_gear(sc, 184, 148, p, t, r=66)
    mc.batt_v(sc, 32, 64, 230, w=6)
    mc.lamps_diamonds(sc, 68, 268, p, 26)
    mc.readout(sc, 184, 268, p, 26, lines=3, pitch=24)
    mc.rotor_reticle(sc, 300, 268, p, t, r=36)


def c27(sc, p, t):          # Target Lock: target viewport
    target_view(sc, (18, 62, 350, 192), p, t, side="RNG 88")            # temp as the range reading
    mc.batt_h(sc, 18, 350, 202, h=4)
    mc.lamps_domes(sc, 68, 262, p, 27, cols=3, rows=2)
    mc.meter_arc(sc, 184, 286, 40, p, 60)
    mc.rotor_radar(sc, 300, 262, p, t, r=40)


def c28(sc, p, t):          # Archive: hex table hero
    mc.scope_rect(sc, (18, 62, 168, 176), p, "ticks", t)
    hex_table(sc, 186, 74, 4, 6, p, 28, temp_cell=(2, 3))               # temp in hex (58 = 88)
    m.notched_frame(sc, 176, 59, 352, 179, p["frame"])
    mc.batt_h(sc, 18, 350, 190)
    mc.lamps_matrix(sc, 68, 256, p, 28)
    bargraph(sc, 140, 218, 228, 296, p, n=8, seed=28)
    mc.rotor_scomp(sc, 300, 256, p, t, r=42)


def c29(sc, p, t):          # Sentinel: hero scomp with voice ring
    edge_strip(sc, 20, 66, 226, p, 29, w=8)
    edge_strip(sc, 340, 66, 226, p, 30, w=8)
    ring_meter(sc, 184, 146, 86, p, 72, n=32, w=7)
    mc.rotor_scomp(sc, 184, 146, p, t, r=70)
    small_temp(sc, 184, 146 + 32, "88", p, size=12)                      # temp under the hub
    mc.batt_v(sc, 36, 66, 226, w=4)
    mc.lamps_pills(sc, 68, 268, p, 29)
    mc.readout(sc, 184, 276, p, 29, lines=2, pitch=24)
    mc.lamps_pills(sc, 300, 268, p, 31)


def c30(sc, p, t):          # Signal Tower: wide spectrum bar graph
    bargraph(sc, 18, 62, 350, 184, p, n=22, seed=30)
    small_temp(sc, 344, 195, "88", p, anchor="rm", size=11)            # temp as the last channel number
    for i, ch in enumerate(("01", "05", "09", "13", "17")):
        sc.text(30 + i * 68, 195, ch, f(m.MONT, 10), p["tick"], "mm")
    mc.batt_h(sc, 18, 290, 205, h=4)
    mc.lamps_grid(sc, 68, 262, p, 30)
    mc.readout(sc, 184, 258, p, 30, lines=4, pitch=29)
    mc.rotor_radar(sc, 300, 262, p, t, r=44)


def c31(sc, p, t):          # Night Watch: tall centre scope, side banks
    vscope(sc, 140, 60, 228, 300, p, t, frame="brackets")
    small_temp(sc, 184, 66, "88", p, size=11)                           # temp in the scope's top tag
    mc.lamps_switcher(sc, 68, 180, p, 31, cols=6, rows=8)
    mc.meter_dots(sc, 300, 60, 300, p, 66, n=16)
    mc.batt_v(sc, 330, 60, 300, w=6)
    mc.rotor_square(sc, 68, 278, p, t, r=26)


def c32(sc, p, t):          # Command Deck: stacked dual scope
    mc.scope_rect(sc, (18, 62, 288, 116), p, "notched", t)
    mc.scope_rect(sc, (18, 126, 288, 180), p, "notched", t + 13, tilt=(-10, 4))
    sc.text(282, 120, "CH 88", f(m.MONT, 10), p["tick"], "rm")         # temp as a channel tag
    mc.meter_vseg(sc, 306, 49, 178, p, 66)
    mc.batt_v(sc, 346, 49, 178)
    mc.lamps_grid(sc, 68, 250, p, 32)
    mc.readout(sc, 184, 246, p, 32, lines=4, pitch=29)
    mc.rotor_gear(sc, 300, 250, p, t, r=44)


def c33(sc, p, t):          # Interceptor Wing: round scope flanked by bar graphs
    mc.scope_round(sc, 184, 136, 74, p, t)
    bargraph(sc, 18, 66, 90, 206, p, n=4, seed=33)
    bargraph(sc, 278, 66, 350, 206, p, n=4, seed=34)
    mc.batt_h(sc, 112, 256, 218, h=4)
    mc.lamps_diamonds(sc, 68, 268, p, 33)
    mc.readout(sc, 184, 266, p, 33, lines=3, pitch=24)
    mc.rotor_square(sc, 300, 268, p, t, r=36)


def c34(sc, p, t):          # Bunker: waterfall + big gauge side by side
    waterfall(sc, 18, 62, 176, 196, p, t, rows=7)
    big_gauge(sc, 268, 168, 78, p, 58)
    mc.batt_h(sc, 192, 344, 188, h=4)
    mc.lamps_switcher(sc, 68, 262, p, 34, cols=6, rows=4)
    hex_table(sc, 158, 244, 2, 3, p, 34, temp_cell=(1, 0), cw=38, rh=18)   # temp in hex (58)
    mc.rotor_scomp(sc, 300, 262, p, t, r=40)


def c35(sc, p, t):          # Cold Steel: wide scope + ring-metered rotor
    edge_strip(sc, 18, 64, 168, p, 35, w=8)
    mc.scope_rect(sc, (36, 62, 350, 168), p, "double", t)
    mc.batt_h(sc, 36, 350, 180, h=4)
    mc.lamps_domes(sc, 68, 258, p, 35, cols=3, rows=3, r=10)
    mc.readout(sc, 184, 258, p, 35, lines=3, pitch=26)
    ring_meter(sc, 300, 258, 46, p, 70, n=22, w=6)
    mc.rotor_scomp(sc, 300, 258, p, t, r=36)
    small_temp(sc, 300, 258 + 18, "88", p, size=10)                     # temp in the rotor hub


def c36(sc, p, t):          # Ember: big gauge left, tall scope right
    big_gauge(sc, 120, 168, 92, p, 70, plate="88")
    vscope(sc, 248, 60, 350, 214, p, t, frame="double")
    mc.batt_h(sc, 28, 212, 208, h=4)
    mc.lamps_pills(sc, 68, 262, p, 36)
    mc.readout(sc, 184, 262, p, 36, lines=3, pitch=24)
    mc.rotor_gear(sc, 300, 262, p, t, r=40)


CONCEPTS2 = [
    (22, "Snow Patrol", "snow", c22, "ring", "ice white + blue; big gauge, temp on the pivot plate"),
    (23, "Desert Outpost", "desert", c23, "strip", "sand + rust; twin scopes, full-width meter"),
    (24, "Forest Scout", "forest", c24, "flat", "tall scope + bar graph; temp as the graph tag"),
    (25, "Dark Ops", "darkops", c25, "flat", "teal on black waterfall; temp as the channel tag"),
    (26, "Lava Forge", "lava", c26, "bevel", "voice ring round a big cog, edge lamp strips"),
    (27, "Target Lock", "targeting", c27, "flat", "locking target view; temp as the range reading"),
    (28, "Archive", "vault", c28, "strip", "hex table; temp hidden in hex (58 = 88)"),
    (29, "Sentinel", "standard", c29, "bevel", "big Scomp with voice ring, temp under the hub"),
    (30, "Signal Tower", "retro", c30, "strip", "wide spectrum bars; temp as the last channel"),
    (31, "Night Watch", "night", c31, "ring", "tall centre scope, lamp bank, dot meter"),
    (32, "Command Deck", "command", c32, "bevel", "stacked twin scopes; temp as a channel tag"),
    (33, "Interceptor Wing", "tie", c33, "pill", "round scope flanked by bar graphs"),
    (34, "Bunker", "olive", c34, "bevel", "waterfall + big gauge; temp in hex"),
    (35, "Cold Steel", "steel", c35, "ring", "wide scope, voice-ringed rotor, temp in its hub"),
    (36, "Ember", "ember", c36, "pill", "big gauge + tall scope; temp on the gauge plate"),
]


def render(fn, pal, keys_style, t):
    p = PAL[pal]
    sc = m.Screen()
    mc.top_bar(sc, p)
    fn(sc, p, t)
    mc.keys(sc, p, keys_style)
    return sc


def main():
    os.makedirs(OUT, exist_ok=True)
    shots = []
    for num, name, pal, fn, ks, note in CONCEPTS2:
        im = render(fn, pal, ks, 1.0 + num * 0.31).finish()
        im.save(os.path.join(OUT, "%02d_%s.png" % (num, name.lower().replace(" ", "_"))))
        shots.append((num, name, note, im))
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


if __name__ == "__main__":
    main()
