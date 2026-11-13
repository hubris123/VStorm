"""Compare voices by tone shape: energy per band while talking, centre, tilt, punch (peak /
average) and noisiness, plus an overlay plot. Used to match the device's radio voicing to the
trooper references.
    python3 tools/voice_analysis/compare.py name=file.wav [name=file2.mp3 ...] [--plot out.png]"""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A  # noqa: E402

SR = 16000
BANDS = [(100, 300), (300, 700), (700, 1500), (1500, 3000), (3000, 5000), (5000, 8000)]


def measure(path):
    x = A.load(path)
    x = x / (np.abs(x).max() + 1e-9)
    win = 512
    fr = np.stack([x[i:i + win] for i in range(0, len(x) - win, 256)])
    e = np.sqrt(np.mean(fr ** 2, 1))
    loud = e > np.percentile(e, 95) * 0.15
    F = fr[loud] * np.hanning(win)
    S = np.abs(np.fft.rfft(F, axis=1)) ** 2
    X = S.mean(0)
    hz = np.arange(len(X)) * SR / win
    tot = X[(hz >= 100) & (hz < 8000)].sum()
    bands = [100 * X[(hz >= lo) & (hz < hi)].sum() / tot for lo, hi in BANDS]
    m = (hz >= 300) & (hz <= 3500)
    centre = float((hz[m] * X[m]).sum() / X[m].sum())
    t = (hz >= 500) & (hz <= 3000)
    tilt = float(np.polyfit(np.log2(hz[t]), 10 * np.log10(X[t] + 1e-12), 1)[0])
    crest = float(np.median(np.abs(fr[loud]).max(1) / (np.sqrt(np.mean(fr[loud] ** 2, 1)) + 1e-9)))
    n = (hz >= 300) & (hz <= 3000)
    flat = float(np.median([np.exp(np.mean(np.log(s[n] + 1e-12))) / np.mean(s[n] + 1e-12) for s in S]))
    return dict(bands=bands, centre=centre, tilt=tilt, crest=crest, noise=flat, X=X, hz=hz)


def plot(res, out):
    from PIL import Image, ImageDraw
    W, H, pad = 900, 420, 55
    im = Image.new("RGB", (W, H), "#0D0F11")
    d = ImageDraw.Draw(im)
    cols = ["#E0A526", "#C4262E", "#459FC2", "#3C8A44", "#C9CDD0"]
    xf = lambda h: pad + (W - 2 * pad) * math.log(h / 100) / math.log(80)
    for f in (200, 300, 500, 1000, 2000, 3000, 5000, 8000):
        d.line([(xf(f), pad), (xf(f), H - pad)], fill="#22272B")
        d.text((xf(f) - 12, H - pad + 8), str(f), fill="#A9B0B5")
    for k, (n, r) in enumerate(res.items()):
        db = 10 * np.log10(r["X"] / r["X"].max() + 1e-12)
        pts = [(xf(h), pad + (H - 2 * pad) * min(1, -v / 60)) for h, v in zip(r["hz"], db) if 100 <= h <= 8000]
        d.line(pts, fill=cols[k % 5], width=2)
        d.text((W - 300, pad + 16 * k), n, fill=cols[k % 5])
    d.text((pad, 16), "average spectrum while talking (dB, 0 = loudest)", fill="#C9CDD0")
    im.save(out)


def main():
    args, out = sys.argv[1:], None
    if "--plot" in args:
        i = args.index("--plot"); out = args[i + 1]; args = args[:i] + args[i + 2:]
    res = {}
    for a in args:
        name, path = a.split("=", 1)
        res[name] = measure(path)
    print("%-22s %s" % ("", " ".join("%9s" % ("%d-%d" % b) for b in BANDS)))
    for n, r in res.items():
        print("%-22s %s" % (n, " ".join("%8.1f%%" % v for v in r["bands"])))
    for n, r in res.items():
        print("%-22s centre %5.0f Hz  tilt %+5.1f dB/oct  peak/avg %4.1f  noisiness %.3f" % (n, r["centre"], r["tilt"], r["crest"], r["noise"]))
    if out:
        plot(res, out)


if __name__ == "__main__":
    main()
