"""Measure the voice "envelope" of reference clips: the target the VOICE shift aims at.

Only numbers come out of this (pitch statistics, formant positions, average spectrum).
The clips themselves stay in voice_refs/ (git-ignored) and never go on the device.

    python3 tools/voice_analysis/analyze.py voice_refs/trooper voice_refs/clone ...
Each argument is a folder of clips (any format ffmpeg reads) = one target. Writes
tools/voice_analysis/report.md, targets.json and spectrum.png.

Method
- decode to mono 16 kHz (the device rate) with ffmpeg
- voiced frames: energy gate + YIN pitch (40 ms frames, 10 ms hop), 70-400 Hz
- pitch: median, 10th / 90th percentile, in Hz and as a semitone spread
- formants F1-F3: LPC (order 12) on voiced frames, roots -> frequencies, median per formant
- long-term average spectrum of voiced frames (the overall tone colour)
"""
import json
import math
import os
import subprocess
import sys

import numpy as np

SR = 16000
HERE = os.path.dirname(os.path.abspath(__file__))
EXTS = (".wav", ".mp3", ".m4a", ".aac", ".flac", ".ogg", ".opus", ".mp4", ".mkv", ".mov", ".webm")


def load(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(SR), "-f", "f32le", "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.float32).astype(np.float64)


def yin(frame, fmin=70, fmax=400, thresh=0.12):
    """YIN pitch of one frame; returns Hz or 0 (unvoiced)"""
    n = len(frame) // 2
    tmin, tmax = SR // fmax, min(SR // fmin, n - 1)
    d = np.array([np.sum((frame[:n] - frame[t:t + n]) ** 2) for t in range(tmax + 1)])
    cmnd = np.ones_like(d)
    cmnd[1:] = d[1:] * np.arange(1, len(d)) / np.maximum(np.cumsum(d[1:]), 1e-12)
    for t in range(tmin, tmax):
        if cmnd[t] < thresh and cmnd[t] <= cmnd[t + 1]:
            a, b, c = cmnd[t - 1], cmnd[t], cmnd[t + 1]              # parabolic refinement
            den = a - 2 * b + c
            tt = t + (0.5 * (a - c) / den if den else 0)
            return SR / tt
    return 0.0


def lpc(x, order=16):
    r = np.correlate(x, x, "full")[len(x) - 1:len(x) + order]
    a, e = np.zeros(order + 1), r[0]
    a[0] = 1.0
    for i in range(1, order + 1):                                   # Levinson-Durbin
        k = -(r[i] + np.dot(a[1:i], r[i - 1:0:-1])) / max(e, 1e-12)
        a[1:i + 1] = a[1:i + 1] + k * np.concatenate((a[i - 1:0:-1], [1.0]))
        e *= 1 - k * k
    return a


def formants(frame):
    x = np.append(frame[0], frame[1:] - 0.63 * frame[:-1]) * np.hamming(len(frame))   # pre-emphasis
    roots = [z for z in np.roots(lpc(x)) if z.imag > 0]
    out = []
    for z in roots:
        f = math.atan2(z.imag, z.real) * SR / (2 * math.pi)
        bw = -SR / math.pi * math.log(abs(z))
        if 200 < f < 4000 and bw < 800:
            out.append(f)
    out.sort()
    picked, lo = [], 0                       # F1 / F2 / F3 each in its usual range, in order
    for rmin, rmax, gap in ((200, 1000, 0), (700, 2700, 200), (1700, 3900, 300)):
        cand = [f for f in out if rmin <= f <= rmax and f > lo + gap]
        picked.append(cand[0] if cand else None)
        if cand:
            lo = cand[0]
    return picked


def analyse(folder):
    files = sorted(os.path.join(folder, f) for f in os.listdir(folder) if f.lower().endswith(EXTS))
    pitches, fms, spec, secs = [], [[], [], []], np.zeros(513), 0.0
    for path in files:
        x = load(path)
        secs += len(x) / SR
        x = x / (np.max(np.abs(x)) + 1e-9)
        win, hop = int(0.04 * SR), int(0.01 * SR)
        energies = [np.sqrt(np.mean(x[i:i + win] ** 2)) for i in range(0, len(x) - win, hop)]
        gate = max(np.percentile(energies, 60) * 0.5, 1e-3) if energies else 1
        for k, i in enumerate(range(0, len(x) - win, hop)):
            fr = x[i:i + win]
            if energies[k] < gate:
                continue
            p = yin(fr)
            if not p:
                continue
            pitches.append(p)
            for j, fq in enumerate(formants(fr[: int(0.025 * SR)])):
                if fq:
                    fms[j].append(fq)
            spec += np.abs(np.fft.rfft(fr * np.hanning(win), 1024)) ** 2
    if not pitches:
        return dict(name=os.path.basename(folder.rstrip("/")), files=len(files), seconds=round(secs, 1), voiced_frames=0)
    p = np.array(pitches)
    med = float(np.median(p))
    spread = float(12 * math.log2(np.percentile(p, 90) / np.percentile(p, 10)))
    ltas = 10 * np.log10(spec / spec.max() + 1e-12)
    hz = np.arange(len(spec)) * SR / 1024
    band = (hz >= 300) & (hz <= 3500)                              # the radio band: where tone lives
    centroid = float(np.sum(hz[band] * spec[band]) / np.sum(spec[band]))
    return dict(name=os.path.basename(folder.rstrip("/")), files=len(files), seconds=round(secs, 1),
                voiced_frames=len(p), pitch_median_hz=round(med, 1),
                pitch_p10_hz=round(float(np.percentile(p, 10)), 1), pitch_p90_hz=round(float(np.percentile(p, 90)), 1),
                pitch_spread_semitones=round(spread, 1),
                tone_centroid_hz=round(centroid, 0),
                formants_hz=[round(float(np.median(v)), 0) if v else None for v in fms],
                ltas_db=[round(float(v), 1) for v in ltas[::8]])


def plot(results, path):
    from PIL import Image, ImageDraw
    W, H, pad = 900, 420, 50
    im = Image.new("RGB", (W, H), "#0D0F11")
    d = ImageDraw.Draw(im)
    cols = ["#E0A526", "#459FC2", "#3C8A44", "#C4262E"]
    for hz in (250, 500, 1000, 2000, 4000, 8000):
        x = pad + (W - 2 * pad) * math.log(hz / 100) / math.log(80)
        d.line([(x, pad), (x, H - pad)], fill="#22272B")
        d.text((x - 12, H - pad + 8), str(hz), fill="#A9B0B5")
    for k, r in enumerate(results):
        if "ltas_db" not in r:
            continue
        v = r["ltas_db"]
        pts = []
        for i, db in enumerate(v):
            hz = i * 8 * SR / 1024
            if hz < 100:
                continue
            pts.append((pad + (W - 2 * pad) * math.log(hz / 100) / math.log(80),
                        pad + (H - 2 * pad) * min(1, -db / 60)))
        d.line(pts, fill=cols[k % 4], width=2)
        d.text((W - 200, pad + 16 * k), r["name"], fill=cols[k % 4])
    d.text((pad, 16), "average spectrum of voiced speech (dB, 0 = loudest)", fill="#C9CDD0")
    im.save(path)


def main():
    results = [analyse(a) for a in sys.argv[1:]]
    with open(os.path.join(HERE, "targets.json"), "w") as fh:
        json.dump(results, fh, indent=1)
    lines = ["# Voice targets (measured)", "", "| target | clips | voiced s | pitch median | 10-90 % range | spread | tone centroid | F1 | F2 | F3 |",
             "|---|---|---|---|---|---|---|---|---|---|"]
    for r in results:
        if not r.get("voiced_frames"):
            lines.append(f"| {r['name']} | {r['files']} | 0 | - | - | - | - | - | - | - |")
            continue
        f1, f2, f3 = (r["formants_hz"] + [None] * 3)[:3]
        lines.append(f"| {r['name']} | {r['files']} | {r['voiced_frames'] / 100:.1f} | {r['pitch_median_hz']} Hz | "
                     f"{r['pitch_p10_hz']}-{r['pitch_p90_hz']} Hz | {r['pitch_spread_semitones']} st | {r['tone_centroid_hz']} Hz | {f1} | {f2} | {f3} |")
    open(os.path.join(HERE, "report.md"), "w").write("\n".join(lines) + "\n")
    plot(results, os.path.join(HERE, "spectrum.png"))
    print("\n".join(lines))


if __name__ == "__main__":
    main()
