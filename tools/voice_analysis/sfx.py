"""Cut the radio key-up / key-down sounds out of the reference clips, for study only.

The snippets go to voice_refs/sfx/ (git-ignored: film audio, never shipped). They are the
training set we measure to synthesise our own key sounds; sfx_report.md + sfx_sheet.png
describe each one (length, loudest frequency, tonal or noisy).

    python3 tools/voice_analysis/sfx.py voice_refs/trooper voice_refs/clone
"""
import os
import sys
import wave

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A  # noqa: E402

SR = A.SR
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "voice_refs", "sfx")
EDGES = False        # also cut each line's first / last 0.3 s (mostly speech; off by default)


def save(path, y):
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((np.clip(y, -1, 1) * 32767).astype("<i2").tobytes())


def describe(y):
    X = np.abs(np.fft.rfft(y * np.hanning(len(y)), 8192)) ** 2
    hz = np.arange(len(X)) * SR / 8192
    peak = float(hz[np.argmax(X[hz > 150]) + np.argmax(hz > 150)])
    flat = float(np.exp(np.mean(np.log(X[hz > 150] + 1e-12))) / (np.mean(X[hz > 150]) + 1e-12))
    centroid = float(np.sum(hz * X) / np.sum(X))
    return peak, flat, centroid


def spectro(y, w=300, h=160):
    win, hop = 256, 32
    F = np.stack([y[i:i + win] * np.hanning(win) for i in range(0, max(1, len(y) - win), hop)])
    M = 20 * np.log10(np.abs(np.fft.rfft(F, axis=1)) + 1e-9).T[::-1]
    M = np.clip((M - M.max() + 60) / 60, 0, 1)
    return Image.fromarray((M * 255).astype(np.uint8)).resize((w, h)).convert("RGB")


def clicks(x, name):
    """radio clicks / crackles: brief spikes of 3-8 kHz energy far above their surroundings"""
    hop = int(0.002 * SR)
    hpf = x - np.convolve(x, np.ones(5) / 5, "same")          # crude high-pass: keeps the crack
    e = np.array([np.sqrt(np.mean(hpf[i:i + hop] ** 2)) for i in range(0, len(x) - hop, hop)])
    k = 50                                                      # 100 ms neighbourhood
    base = np.array([np.median(e[max(0, i - k):i + k]) for i in range(len(e))]) + 1e-6
    hits, i = [], 0
    while i < len(e):
        if e[i] > 6 * base[i] and e[i] > np.percentile(e, 90):
            j = i
            while j < len(e) and e[j] > 3 * base[j]:
                j += 1
            if (j - i) * 2 <= 30:                               # short = a click, not speech
                hits.append(i * hop)
            i = j + 25
        else:
            i += 1
    out = []
    for n, c in enumerate(hits):
        y = x[max(0, c - int(0.03 * SR)):c + int(0.09 * SR)]
        out.append((f"{name}_click{n + 1:02d}_{c / SR:05.1f}s", y))
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    rows, tiles = [], []
    for folder in sys.argv[1:]:
        name = os.path.basename(folder.rstrip("/"))
        for f in sorted(os.listdir(folder)):
            if not f.lower().endswith(A.EXTS):
                continue
            x = A.load(os.path.join(folder, f))
            x = x / (np.abs(x).max() + 1e-9)
            win, hop = 640, 160
            idx = np.arange(0, len(x) - win, hop)
            e = np.sqrt(np.mean(np.stack([x[i:i + win] for i in idx]) ** 2, axis=1))
            loud = e > max(np.percentile(e, 95) * 0.12, 1e-3)
            for tag, y in clicks(x, name):
                save(os.path.join(OUT, tag + ".wav"), y)
                peak, flat, cen = describe(y)
                rows.append((tag, len(y) / SR, peak, flat, cen))
                tiles.append((tag, spectro(y)))
            for n, (a, b) in enumerate(A.segments(loud, trim_s=0.0) if EDGES else []):
                s0, s1 = idx[a], idx[b] + win
                for kind, lo, hi in (("up", s0 - int(0.05 * SR), s0 + int(0.30 * SR)),
                                     ("down", s1 - int(0.30 * SR), s1 + int(0.15 * SR))):
                    y = x[max(0, lo):min(len(x), hi)]
                    if len(y) < 512:
                        continue
                    tag = f"{name}_{n + 1:02d}_{kind}"
                    save(os.path.join(OUT, tag + ".wav"), y)
                    peak, flat, cen = describe(y)
                    rows.append((tag, len(y) / SR, peak, flat, cen))
                    tiles.append((tag, spectro(y)))
    lines = ["# Radio key sounds (study set, not shipped)", "",
             "Each line's first 0.30 s (key-up) and last 0.30 s (key-down) with a little lead-in / tail.",
             "flatness: 0 = pure tone (beep), 1 = white noise (static / click).", "",
             "| snippet | length | loudest | flatness | centroid |", "|---|---|---|---|---|"]
    for tag, ln, pk, fl, ce in rows:
        lines.append(f"| {tag} | {ln:.2f} s | {pk:.0f} Hz | {fl:.2f} | {ce:.0f} Hz |")
    open(os.path.join(HERE, "sfx_report.md"), "w").write("\n".join(lines) + "\n")
    cols = 8
    W = Image.new("RGB", (cols * 310 + 10, ((len(tiles) + cols - 1) // cols) * 185 + 10), "#0D0F11")
    d = ImageDraw.Draw(W)
    for k, (tag, im) in enumerate(tiles):
        x0, y0 = 10 + (k % cols) * 310, 10 + (k // cols) * 185
        W.paste(im, (x0, y0))
        d.text((x0, y0 + 164), tag, fill="#E0A526")
    W.save(os.path.join(HERE, "sfx_sheet.png"))
    print(f"cut {len(rows)} snippets to {os.path.normpath(OUT)}")


if __name__ == "__main__":
    main()
