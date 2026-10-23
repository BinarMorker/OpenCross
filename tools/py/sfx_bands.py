"""Per-window band energy (dB) PS1 vs ours. usage: sfx_bands.py raw t_event ours.wav t0_ms t1_ms win_ms"""
import sys, os, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from x2_audio import load, load_wav
raw, t, w = sys.argv[1], float(sys.argv[2]), sys.argv[3]
t0, t1, win = float(sys.argv[4]), float(sys.argv[5]), float(sys.argv[6])
p = load(raw).mean(1); o = load_wav(w)[0].mean(1)
n = int(win * 44.1); f = np.fft.rfftfreq(n, 1 / 44100)
bands = [(0, 300), (300, 1000), (1000, 3000), (3000, 8000), (8000, 22050)]
def be(x):
    S = np.abs(np.fft.rfft(x * np.hanning(n))) ** 2
    return [10 * np.log10(S[(f >= a) & (f < b)].sum() + 1e-9) for a, b in bands]
print("t_ms | ps1 dB per band " + str(bands) + " | ours - ps1")
for ms in np.arange(t0, t1, win):
    a = p[int(t * 44100 + ms * 44.1):][:n]; b = o[int(ms * 44.1):][:n]
    pa, ob = be(a), be(b)
    print(f"{ms:6.0f} | " + " ".join(f"{x:5.1f}" for x in pa) + " | " + " ".join(f"{y-x:+5.1f}" for x, y in zip(pa, ob)))
