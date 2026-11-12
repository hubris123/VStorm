"""Synthetic test voices with known pitch and formants (cascade formant synthesis):
male 115 Hz, female 210 Hz (formants x1.17), child 290 Hz (x1.3). Syllables, vowel changes,
intonation and pauses, 16 kHz mono WAV. Not real speech: for checking the shift numbers.
    python3 tools/dsp_test/make_test_voices.py <out dir>"""
import math, os, sys, wave
import numpy as np
SR = 16000
VOWELS = [(730, 1090, 2440), (270, 2290, 3010), (530, 1840, 2480), (570, 840, 2410), (300, 870, 2240),
          (660, 1720, 2410), (490, 1350, 1690)]

def reson(x, f, bw):
    r = math.exp(-math.pi * bw / SR); c = 2 * r * math.cos(2 * math.pi * f / SR)
    y = np.zeros_like(x); a = b = 0.0
    for i, v in enumerate(x):
        yy = v + c * a - r * r * b; b = a; a = yy; y[i] = yy * (1 - r * r)
    return y

def voice(f0, scale, secs=12, seed=1):
    rnd = np.random.default_rng(seed)
    n = int(SR * secs); t = np.arange(n) / SR
    # phrases of 1.5-2.5 s with 0.4 s pauses; syllables at ~4.5 Hz
    env = np.zeros(n); pitch = np.full(n, f0); vow = np.zeros(n, int)
    pos = 0
    while pos < n:
        L = int(SR * rnd.uniform(1.5, 2.5)); seg = slice(pos, min(n, pos + L))
        tt = np.arange(seg.stop - seg.start) / SR
        syl = (tt * 4.5) % 1
        env[seg] = np.sin(np.pi * syl) ** 0.5
        pitch[seg] = f0 * (1 + 0.12 * np.sin(2 * np.pi * 0.6 * tt + rnd.uniform(0, 6))) * (1 - 0.15 * tt / tt[-1])
        vow[seg] = (np.floor(tt * 4.5).astype(int) * 3 + rnd.integers(0, 7)) % len(VOWELS)
        pos += L + int(SR * 0.4)
    ph = np.cumsum(pitch / SR)
    src = np.where((ph % 1) < 0.08, 1.0, -0.087) * env + rnd.normal(0, 0.002, n)
    out = np.zeros(n)
    # vowels change per syllable: synthesise each vowel track and pick per sample (crossfaded)
    for k, (f1, f2, f3) in enumerate(VOWELS):
        mask = (vow == k).astype(float)
        mask = np.convolve(mask, np.ones(160) / 160, "same")
        if mask.max() == 0: continue
        y = src.copy()
        for F in (f1 * scale, f2 * scale, f3 * scale, 3500 * scale):
            y = reson(y, min(F, 7000), 60 + F * 0.04)
        out += y * mask
    return out / np.abs(out).max() * 0.7

def save(path, y):
    w = wave.open(path, "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((np.clip(y, -1, 1) * 32767).astype("<i2").tobytes()); w.close()

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    for name, f0, sc in (("male", 115, 1.0), ("female", 210, 1.17), ("child", 290, 1.3)):
        save(os.path.join(out, name + ".wav"), voice(f0, sc))
        print("wrote", name)
