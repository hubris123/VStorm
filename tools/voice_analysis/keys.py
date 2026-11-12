"""Measure the radio key / noise reference sounds (study set in voice_refs/keys/, git-ignored).

For each sound: length, loudness envelope (10 ms steps), where the main click sits, the ring
frequency after the click and how fast it dies away, the noise band, and how tonal it is.
These numbers drive our own synthesised key sounds; the reference audio is never shipped.

    python3 tools/voice_analysis/keys.py voice_refs/keys
writes tools/voice_analysis/keys_report.md (numbers, committed) and keys_sheet.png (local)
"""
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

SR = 44100
HERE = os.path.dirname(os.path.abspath(__file__))


def load(p):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", p, "-ac", "1", "-ar", str(SR), "-f", "f32le", "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.float32).astype(float)


def measure(x):
    step = SR // 100                                                   # 10 ms
    env = np.array([np.sqrt(np.mean(x[i:i + step] ** 2)) for i in range(0, len(x) - step + 1, step)])
    db = 20 * np.log10(env / (env.max() + 1e-12) + 1e-9)
    floor = float(np.percentile(db, 20))
    pk = int(np.argmax(env))
    # click: the sharpest jump in short-time energy (1 ms)
    e1 = np.array([np.sum(x[i:i + 44] ** 2) for i in range(0, len(x) - 44, 44)])
    jump = np.diff(np.log(e1 + 1e-12))
    click_ms = float(np.argmax(jump) + 1)
    c = int(click_ms * 44)
    # ring after the click: dominant frequency in the next 30 ms and its decay time (-20 dB)
    tail = x[c:c + int(0.03 * SR)]
    ring_hz = 0.0
    if len(tail) > 256:
        X = np.abs(np.fft.rfft(tail * np.hanning(len(tail)), 16384))
        hz = np.arange(len(X)) * SR / 16384
        m = hz > 200
        ring_hz = float(hz[m][np.argmax(X[m])])
    after = db[pk:]
    decay_ms = float(np.argmax(after < db[pk] - 20) * 10) if (after < db[pk] - 20).any() else float(len(after) * 10)
    # overall spectrum: centroid, 90 % energy band edges, tonality
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)), 65536)) ** 2
    hz = np.arange(len(X)) * SR / 65536
    m = hz > 100
    cum = np.cumsum(X[m]) / np.sum(X[m])
    lo, hi = float(hz[m][np.searchsorted(cum, 0.05)]), float(hz[m][np.searchsorted(cum, 0.95)])
    cen = float(np.sum(hz * X) / np.sum(X))
    flat = float(np.exp(np.mean(np.log(X[m] + 1e-18))) / np.mean(X[m]))
    return dict(ms=round(len(x) / SR * 1000), peak_ms=pk * 10, click_ms=round(click_ms), ring_hz=round(ring_hz),
                decay_ms=round(decay_ms), band=(round(lo), round(hi)), centroid=round(cen), flat=round(flat, 3),
                floor_db=round(floor), env=[int(v) for v in db])


def tile(x, title, w=480, h=240):
    win, hop = 512, 32
    F = np.stack([x[i:i + win] * np.hanning(win) for i in range(0, max(1, len(x) - win), hop)])
    M = 20 * np.log10(np.abs(np.fft.rfft(F, axis=1)) + 1e-9).T[::-1]
    M = np.clip((M - M.max() + 70) / 70, 0, 1)
    im = Image.fromarray((M * 255).astype(np.uint8)).resize((w, h)).convert("RGB")
    W = Image.new("RGB", (w, h + 70), "#111")
    W.paste(im, (0, 0))
    d = ImageDraw.Draw(W)
    for k in (2, 4, 8, 12, 16, 20):
        y = h - h * k * 1000 / (SR / 2)
        d.text((2, y - 6), f"{k}k", fill=(255, 90, 90))
    env = [np.sqrt(np.mean(x[i:i + 441] ** 2)) for i in range(0, len(x) - 441, 441)]
    mx = max(env) + 1e-12
    pts = [(i * w / len(env), h + 66 - 60 * max(0, 1 + 20 * np.log10(v / mx + 1e-9) / 60)) for i, v in enumerate(env)]
    d.line(pts, fill=(224, 165, 38), width=2)
    d.text((w // 2 - 60, h + 2), title, fill=(255, 255, 0))
    return W


def main():
    folder = sys.argv[1]
    files = sorted(f for f in os.listdir(folder) if f.lower().endswith((".wav", ".flac")))
    rows, tiles = [], []
    for f in files:
        x = load(os.path.join(folder, f))
        r = measure(x)
        rows.append((f, r))
        tiles.append(tile(x, f"{f}  {r['ms']} ms"))
    lines = ["# Radio key sounds: measurements (reference audio stays local, never shipped)", "",
             "click = sharpest onset; ring = loudest tone in the 30 ms after it; decay = time to fall 20 dB",
             "after the loudest 10 ms; band = 5-95 % of the energy; flatness 0 = pure tone, 1 = white noise.", "",
             "| sound | length | click at | ring | decay | band | centroid | flatness | floor |",
             "|---|---|---|---|---|---|---|---|---|"]
    for f, r in rows:
        lines.append(f"| {f} | {r['ms']} ms | {r['click_ms']} ms | {r['ring_hz']} Hz | {r['decay_ms']} ms | "
                     f"{r['band'][0]}-{r['band'][1]} Hz | {r['centroid']} Hz | {r['flat']} | {r['floor_db']} dB |")
    open(os.path.join(HERE, "keys_report.md"), "w").write("\n".join(lines) + "\n")
    cols = 4
    G = Image.new("RGB", (cols * 490 + 10, ((len(tiles) + cols - 1) // cols) * 320 + 10), "#000")
    for k, t in enumerate(tiles):
        G.paste(t, (10 + (k % cols) * 490, 10 + (k // cols) * 320))
    G.save(os.path.join(HERE, "keys_sheet.png"))
    print("\n".join(lines))


if __name__ == "__main__":
    main()
