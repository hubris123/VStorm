"""Image mock-ups of the VOICE settings page (page 2): learn the wearer's voice, then shift
it to the TROOPER or CLONE target with a fixed, known shift.

Screens: setup (no voice learned yet), reading (scrolling script, live shift), result
(accept / retry / adjust), adjust (PITCH and TONE by ear).

Wording rules: say TROOPER (never the franchise name), no main character names.

    python3 design/mockups/make_voice_mockups.py
writes design/mockups/voice/*.png + voice_sheet.png
"""
import os

from PIL import Image, ImageDraw, ImageFont

import make_mockups as m

S, f = m.S, m.font
OUT = os.path.join(m.HERE, "voice")
PAGES = 12

# reading script: short report lines (no character names)
SCRIPT = [
    "Patrol seven, reporting in.",
    "Sector four is clear.",
    "Move along. Move along.",
    "Let me see your identification.",
    "Close the blast doors!",
    "Set your blasters to stun.",
    "We have them on the scanners.",
    "Squad, form up on me.",
]

# target bands (placeholders until the reference recordings are measured)
TARGET = {"TROOPER": (100, 125), "CLONE": (110, 135)}


def header(sc, title, page=2):
    sc.text(46, 28, title, f(m.MONT, 22), m.TEXT, "lm")
    sc.text(322, 28, f"{page}/{PAGES}", f(m.MONT, 16), m.LABEL, "rm")
    for i in range(PAGES):
        x = 46 + i * 23
        sc.rect(x, 46, x + 18, 50, fill=m.AMBER if i + 1 == page else m.DIM)


def hz_x(hz, x0=40, x1=328):
    """log pitch scale 70..320 Hz"""
    import math
    return x0 + (x1 - x0) * (math.log(hz) - math.log(70)) / (math.log(320) - math.log(70))


def voice_map(sc, y, you=None, target="TROOPER", shifted=None, label=True):
    """pitch scale with the target band, the wearer's pitch and (optionally) where it lands"""
    lo, hi = TARGET[target]
    sc.rect(40, y - 3, 328, y + 3, fill="#1A1F24")
    sc.rect(hz_x(lo), y - 9, hz_x(hi), y + 9, fill=m.mix(m.GREEN, "#000000", 0.45))
    sc.d.rectangle(sc.b(hz_x(lo), y - 9, hz_x(hi), y + 9), outline=m.GREEN, width=S)
    for hz in (80, 120, 160, 200, 250, 300):
        x = hz_x(hz)
        sc.line([(x, y + 12), (x, y + 17)], m.FRAME, 1)
        sc.text(x, y + 26, str(hz), f(m.MONT, 11), m.LABEL, "mm")
    if label:
        sc.text(hz_x(lo) + (hz_x(hi) - hz_x(lo)) / 2, y - 18, target, f(m.MONT, 12), m.GREEN, "mm")
    if you:
        x = hz_x(you)
        sc.d.polygon(sc.b(x - 7, y - 20, x + 7, y - 20, x, y - 9), fill=m.AMBER)
        sc.text(x, y - 30, "YOU", f(m.MONT, 12), m.AMBER, "mm")
    if you and shifted:
        x0, x1 = hz_x(you), hz_x(shifted)
        sc.line([(x0, y), (x1 + 8, y)], m.AMBER, 2)
        sc.d.polygon(sc.b(x1 + 8, y - 5, x1 + 8, y + 5, x1, y), fill=m.AMBER)


def key_fit(sc, x, y, w, h, label, accent, sub=None, lit=False):
    """a key whose label shrinks to fit the cap (long words like TROOPER)"""
    m.key(sc, x, y, w, h, " ", accent, sub=sub, lit=lit)
    px = 18
    while px > 13 and f(m.MONT, px).getlength(label) / S > w - 30:
        px -= 1
    ty = y + h / 2 - (10 if sub else 0)
    sc.text(x + w / 2, ty, label, f(m.MONT, px), "#FFF4F0" if lit else accent, "mm")


def nav(sc):
    m.key(sc, 16, 320, 104, 114, "◀", m.BLUE, sub="PREV", big=True)
    m.key(sc, 132, 320, 104, 114, "MAIN", m.TEXT, sub="EXIT")
    m.key(sc, 248, 320, 104, 114, "▶", m.BLUE, sub="NEXT", big=True)


def setup_screen(learned=False):
    sc = m.Screen()
    header(sc, "VOICE")
    m.notched_frame(sc, 16, 60, 352, 192)
    sc.text(32, 80, "AUTO VOICE", f(m.MONT, 14), m.TEXT, "lm")
    sc.text(336, 80, "ON" if learned else "NOT LEARNED", f(m.MONT, 14), m.GREEN if learned else m.AMBER, "rm")
    voice_map(sc, 132, you=196 if learned else None, shifted=112 if learned else None)
    if learned:
        sc.text(32, 178, "PITCH  -9.5 ST", f(m.MONT, 13), m.LABEL, "lm")
        sc.text(336, 178, "TONE  -18 %", f(m.MONT, 13), m.LABEL, "rm")
    else:
        sc.text(184, 178, "Read the script once to learn your voice", f(m.MONT, 13), m.LABEL, "mm")
    # row A: AUTO | TARGET | LEARN (bottom-row columns)
    if learned:
        m.key(sc, 16, 204, 104, 104, "AUTO", m.GREEN, lit=True, sub="ON")
    else:
        m.key(sc, 16, 204, 104, 104, "AUTO", m.DIM, sub="LEARN FIRST")
    key_fit(sc, 132, 204, 104, 104, "TROOPER", m.BLUE, sub="TARGET")
    m.key(sc, 248, 204, 104, 104, "LEARN", m.AMBER, sub="READ SCRIPT", lit=not learned)
    nav(sc)
    return sc


def reading_screen(line=2, secs=8, you=188, now=131):
    sc = m.Screen()
    header(sc, "LEARN VOICE")
    m.notched_frame(sc, 16, 60, 352, 300)
    sc.text(32, 78, "READ ALOUD", f(m.MONT, 12), m.LABEL, "lm")
    sc.text(336, 78, f"{secs} / 15 S", f(m.MONT, 12), m.LABEL, "rm")
    # teleprompter: the current line big and bright on a bar, the rest dimmer
    for k in range(-1, 2):
        i = line + k
        if not 0 <= i < len(m_script()):
            continue
        y = 142 + k * 36
        if k == 0:
            sc.rect(24, y - 17, 344, y + 17, fill="#16202A")
            sc.rect(24, y - 17, 28, y + 17, fill=m.AMBER)
        col = m.TEXT if k == 0 else m.mix(m.TEXT, "#000000", 0.45 + 0.2 * abs(k))
        size = 20 if k == 0 else 15
        txt = m_script()[i]
        while f(m.MONT, size).getlength(txt) / S > 300 and size > 13:
            size -= 1
        sc.text(184, y, txt, f(m.MONT, size), col, "mm")
    # live voice map: where the wearer is now and where the shift is taking them
    voice_map(sc, 238, you=you, shifted=now, label=True)
    # progress
    for i in range(15):
        sc.rect(24 + i * 21, 286, 40 + i * 21, 290, fill=m.AMBER if i < secs else m.DIM)
    m.key(sc, 16, 320, 104, 114, "STOP", m.RED, sub="CANCEL")
    m.key(sc, 132, 320, 104, 114, "PAUSE", m.TEXT, sub="HOLD")
    m.key(sc, 248, 320, 104, 114, "DONE", m.GREEN, sub="FINISH")
    return sc


def m_script():
    return SCRIPT


def result_screen():
    sc = m.Screen()
    header(sc, "VOICE LEARNED")
    m.notched_frame(sc, 16, 60, 352, 300)
    sc.text(184, 86, "LISTEN: YOUR VOICE IS LIVE", f(m.MONT, 13), m.GREEN, "mm")
    sc.text(100, 122, "YOU", f(m.MONT, 12), m.LABEL, "mm")
    sc.text(100, 146, "196 HZ", f(m.MONT, 22), m.AMBER, "mm")
    sc.text(268, 122, "TROOPER", f(m.MONT, 12), m.LABEL, "mm")
    sc.text(268, 146, "112 HZ", f(m.MONT, 22), m.GREEN, "mm")
    sc.d.polygon(sc.b(196, 138, 196, 154, 210, 146), fill=m.AMBER)
    sc.line([(150, 146), (196, 146)], m.AMBER, 2)
    sc.text(100, 182, "PITCH  -9.5 ST", f(m.MONT, 14), m.TEXT, "mm")
    sc.text(268, 182, "TONE  -18 %", f(m.MONT, 14), m.TEXT, "mm")
    voice_map(sc, 248, you=196, shifted=112)
    m.key(sc, 16, 320, 104, 114, "RETRY", m.AMBER, sub="READ AGAIN")
    m.key(sc, 132, 320, 104, 114, "ACCEPT", m.GREEN, lit=True, sub="SAVE")
    m.key(sc, 248, 320, 104, 114, "ADJUST", m.BLUE, sub="BY EAR")
    return sc


def adjust_screen(sel="PITCH"):
    sc = m.Screen()
    header(sc, "ADJUST VOICE")
    m.notched_frame(sc, 16, 60, 352, 192)
    for k, (name, val, pct) in enumerate((("PITCH", "-9.5 ST", 0.62), ("TONE", "-18 %", 0.45))):
        y = 92 + k * 54
        on = name == sel
        if on:
            sc.rect(24, y - 20, 344, y + 20, fill="#16202A")
            sc.rect(24, y - 20, 28, y + 20, fill=m.AMBER)
        sc.text(40, y - 6, name, f(m.MONT, 16), m.TEXT if on else m.LABEL, "lm")
        sc.text(40, y + 12, "HIGHER / LOWER" if name == "PITCH" else "SMALLER / BIGGER", f(m.MONT, 11), m.LABEL, "lm")
        sc.text(336, y, val, f(m.MONT, 20), m.AMBER if on else m.LABEL, "rm")
        for i in range(10):
            sc.rect(170 + i * 9, y - 4, 176 + i * 9, y + 4, fill=(m.AMBER if on else m.LABEL) if i < pct * 10 else m.DIM)
    sc.text(184, 176, "LIVE: CHANGES ARE HEARD AT ONCE", f(m.MONT, 12), m.GREEN, "mm")
    m.key(sc, 16, 204, 104, 104, "−", m.BLUE, sub=sel, big=True)
    m.key(sc, 132, 204, 104, 104, "TONE" if sel == "PITCH" else "PITCH", m.TEXT, sub="SELECT")
    m.key(sc, 248, 204, 104, 104, "+", m.BLUE, sub=sel, big=True)
    m.key(sc, 16, 320, 104, 114, "◀", m.BLUE, sub="BACK", big=True)
    m.key(sc, 132, 320, 104, 114, "SAVE", m.GREEN, lit=True, sub="KEEP")
    m.key(sc, 248, 320, 104, 114, "RESET", m.AMBER, sub="AS LEARNED")
    return sc


SCREENS = [
    ("01_voice_not_learned", "VOICE page, first time", setup_screen(False)),
    ("02_learn_reading", "Learning: read the script", reading_screen()),
    ("03_voice_learned", "Result: accept, retry, adjust", result_screen()),
    ("04_adjust", "Adjust by ear", adjust_screen()),
    ("05_voice_on", "VOICE page, learned + AUTO on", setup_screen(True)),
]


def main():
    os.makedirs(OUT, exist_ok=True)
    ims = []
    for name, cap, sc in SCREENS:
        im = sc.finish()
        im.save(os.path.join(OUT, name + ".png"))
        ims.append((cap, im))
    tw, th = ims[0][1].width // 2, ims[0][1].height // 2
    cf = ImageFont.truetype(m.MONT, 22)
    W = Image.new("RGB", (len(ims) * (tw + 20) + 20, th + 70), "#0D0F11")
    d = ImageDraw.Draw(W)
    for k, (cap, im) in enumerate(ims):
        x = 20 + k * (tw + 20)
        W.paste(im.resize((tw, th), Image.LANCZOS), (x, 20))
        d.text((x + 4, th + 32), f"{k + 1}  {cap}", font=cf, fill="#E8E8E8")
    W.save(os.path.join(OUT, "voice_sheet.png"))
    print("wrote", len(ims), "voice screens to", OUT)


if __name__ == "__main__":
    main()
