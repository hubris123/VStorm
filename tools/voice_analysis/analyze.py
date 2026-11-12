"""Measure the voice "envelope" of reference clips: the target the VOICE shift aims at.

Only numbers come out of this (pitch statistics, formant positions, average spectrum).
The clips themselves stay in voice_refs/ (git-ignored) and never go on the device.

    python3 tools/voice_analysis/analyze.py voice_refs/trooper voice_refs/clone ...
Each argument is a folder of clips (any format ffmpeg reads) = one target. Writes
tools/voice_analysis/report.md, targets.json and spectrum.png.

Method
- decode to mono 16 kHz (the device rate) with ffmpeg
- spoken lines found by loudness; the edges of each line are trimmed (radio key-up / key-down)
- pitch from the harmonic spacing in 250-1500 Hz (64 ms frames, 10 ms hop): survives radio
  filtering (no fundamental) and mains hum; steady tones (beeps) are dropped
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


def lowband(x):
    """60-1000 Hz: the fundamental and first harmonics, where pitch is easiest to see"""
    h = np.sinc(2 * 1000 / SR * (np.arange(129) - 64)) * np.hamming(129)
    y = np.convolve(x, h / h.sum(), "same")
    return y - np.convolve(y, np.ones(160) / 160, "same")


def spacing_pitch(frames, fmin=70, fmax=300, band=(250, 1500), nfft=4096):
    """pitch from the spacing of the harmonics in the 250-1500 Hz band -> (Hz, clarity).
    Radio-filtered voices have lost their fundamental, and film audio has mains hum below
    ~400 Hz; the harmonic spacing in the speech band survives both."""
    win = frames.shape[1]
    X = np.abs(np.fft.rfft(frames * np.hanning(win), nfft, axis=1))
    df = SR / nfft
    lo, hi = int(band[0] / df), int(band[1] / df)
    B = np.log1p(X[:, lo:hi] / (X[:, lo:hi].mean(axis=1, keepdims=True) + 1e-12))
    k = int(150 / df)                                      # remove the slow envelope (formants)
    B = B - np.apply_along_axis(lambda v: np.convolve(v, np.ones(k) / k, "same"), 1, B)
    lmin, lmax = int(fmin / df), int(fmax / df) + 1
    n = B.shape[1] - lmax
    a = B[:, :n]
    ea = np.sum(a * a, axis=1) + 1e-12
    r = np.empty((len(B), lmax - lmin))
    for j, t in enumerate(range(lmin, lmax)):
        bb = B[:, t:t + n]
        r[:, j] = np.sum(a * bb, axis=1) / np.sqrt(ea * (np.sum(bb * bb, axis=1) + 1e-12))
    best = r.max(axis=1)
    lag = np.empty(len(B))
    for i in range(len(B)):                               # earliest strong peak = the true spacing
        ri = r[i]
        cand = np.where((ri[1:-1] > 0.8 * best[i]) & (ri[1:-1] >= ri[:-2]) & (ri[1:-1] >= ri[2:]))[0]
        j = (1 + cand[0]) if len(cand) else int(np.argmax(ri))
        if 0 < j < len(ri) - 1:                           # parabolic refinement
            d = ri[j - 1] - 2 * ri[j] + ri[j + 1]
            j = j + (0.5 * (ri[j - 1] - ri[j + 1]) / d if d else 0)
        lag[i] = lmin + j
    return lag * df, best


def segments(e, hop_s=0.01, gap_s=0.15, trim_s=0.12, min_s=0.3):
    """spoken lines: runs of loud frames (short gaps merged), with the edges trimmed off so
    radio key-up / key-down clicks and beeps at the start and end of a line are left out"""
    on = np.where(e)[0]
    if not len(on):
        return []
    runs, s0, prev = [], on[0], on[0]
    for i in on[1:]:
        if i - prev > gap_s / hop_s:
            runs.append((s0, prev)); s0 = i
        prev = i
    runs.append((s0, prev))
    t = int(trim_s / hop_s)
    return [(a + t, b - t) for a, b in runs if (b - a) * hop_s >= min_s + 2 * trim_s]


def analyse(folder):
    files = sorted(os.path.join(folder, f) for f in os.listdir(folder) if f.lower().endswith(EXTS))
    pitches, fms, spec, secs, nlines, ntone = [], [[], [], []], np.zeros(513), 0.0, 0, 0
    win, hop = int(0.04 * SR), int(0.01 * SR)
    for path in files:
        x = load(path)
        secs += len(x) / SR
        x = x / (np.max(np.abs(x)) + 1e-9)
        idx = np.arange(0, len(x) - win, hop)
        frames = np.stack([x[i:i + win] for i in idx])
        e = np.sqrt(np.mean(frames ** 2, axis=1))
        loud = e > max(np.percentile(e, 95) * 0.12, 1e-3)      # within ~18 dB of the loud parts
        lines = segments(loud)
        nlines += len(lines)
        keep = np.zeros(len(idx), bool)
        for a, b in lines:
            keep[a:b + 1] = True
        keep &= loud
        f0, clar = spacing_pitch(np.stack([x[i:i + 1024] for i in idx if i + 1024 <= len(x)]))
        f0 = np.resize(f0, len(idx)); clar = np.resize(clar, len(idx))
        voiced = keep & (clar > 0.35)
        # beeps: a pitch held dead steady (< 0.5 % change over 80 ms) is a tone, not a voice
        for k in range(len(f0) - 8):
            seg = f0[k:k + 8]
            if voiced[k:k + 8].all() and seg.max() / seg.min() < 1.005:
                voiced[k:k + 8] = False
                ntone += 1
        for k in np.where(voiced)[0]:
            pitches.append(f0[k])
            fr = frames[k]
            for j, fq in enumerate(formants(fr[: int(0.025 * SR)])):
                if fq:
                    fms[j].append(fq)
            spec += np.abs(np.fft.rfft(fr * np.hanning(win), 1024)) ** 2
    name = os.path.basename(folder.rstrip("/"))
    if not pitches:
        return dict(name=name, files=len(files), seconds=round(secs, 1), voiced_frames=0)
    p = np.array(pitches)
    # centre = the most common pitch (smoothed histogram, half-semitone bins)
    st = 12 * np.log2(p / 100)
    h, edges = np.histogram(st, bins=np.arange(-6, 24, 0.5))
    h = np.convolve(h, [1, 2, 3, 2, 1], "same")
    mode = 100 * 2 ** ((edges[np.argmax(h)] + 0.25) / 12)
    # fold octave errors (about 2x or 1/2 the centre, +-2 semitones) back to the right octave
    folded = 0
    for k, v in enumerate(p):
        d = 12 * math.log2(v / mode)
        for oct_ in (12, -12, 19):
            if abs(d - oct_) < 2:
                p[k] = v / 2 ** (oct_ / 12)
                folded += 1
                break
    med = float(np.median(p))
    spread = float(12 * math.log2(np.percentile(p, 90) / np.percentile(p, 10)))
    ltas = 10 * np.log10(spec / spec.max() + 1e-12)
    hz = np.arange(len(spec)) * SR / 1024
    band = (hz >= 300) & (hz <= 3500)                              # the radio band: where tone lives
    centroid = float(np.sum(hz[band] * spec[band]) / np.sum(spec[band]))
    return dict(name=name, files=len(files), seconds=round(secs, 1), lines=nlines,
                voiced_frames=len(p), beep_frames_dropped=ntone, octave_fixed=folded,
                pitch_centre_hz=round(float(mode), 1), pitch_median_hz=round(med, 1),
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
    lines = ["# Voice targets (measured)", "", "| target | lines | voiced s | pitch centre | median | 10-90 % range | spread | tone centroid | F1 | F2 | F3 |",
             "|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in results:
        if not r.get("voiced_frames"):
            lines.append(f"| {r['name']} | 0 | 0 | - | - | - | - | - | - | - | - |")
            continue
        f1, f2, f3 = (r["formants_hz"] + [None] * 3)[:3]
        lines.append(f"| {r['name']} | {r['lines']} | {r['voiced_frames'] / 100:.1f} | {r['pitch_centre_hz']} Hz | {r['pitch_median_hz']} Hz | "
                     f"{r['pitch_p10_hz']}-{r['pitch_p90_hz']} Hz | {r['pitch_spread_semitones']} st | {r['tone_centroid_hz']} Hz | {f1} | {f2} | {f3} |")
    open(os.path.join(HERE, "report.md"), "w").write("\n".join(lines) + "\n")
    plot(results, os.path.join(HERE, "spectrum.png"))
    print("\n".join(lines))


if __name__ == "__main__":
    main()
