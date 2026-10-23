"""Band energies (dB) PS1 vs ours over a segment. usage: sfx_spec.py raw t_event ours.wav t0_ms t1_ms"""
import sys, os, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from x2_audio import load, load_wav
raw, t, w = sys.argv[1], float(sys.argv[2]), sys.argv[3]
t0, t1 = float(sys.argv[4]) / 1000, float(sys.argv[5]) / 1000
p = load(raw).mean(1); o = load_wav(w)[0].mean(1)
a = p[int((t + t0) * 44100):int((t + t1) * 44100)]; b = o[int(t0 * 44100):int(t1 * 44100)]
n = 1 << int(np.ceil(np.log2(len(a))))
fa = np.abs(np.fft.rfft(a * np.hanning(len(a)), n)) ** 2; fb = np.abs(np.fft.rfft(b * np.hanning(len(b)), n)) ** 2
f = np.fft.rfftfreq(n, 1 / 44100)
edges = [0, 150, 300, 600, 1000, 1500, 2000, 3000, 4000, 6000, 8000, 11000, 16000, 22050]
print(" band Hz        ps1 dB   ours dB   diff")
for lo, hi in zip(edges, edges[1:]):
    m = (f >= lo) & (f < hi)
    da, db = 10 * np.log10(fa[m].sum() + 1e-12), 10 * np.log10(fb[m].sum() + 1e-12)
    print(f"{lo:6d}-{hi:6d}  {da:8.1f} {db:8.1f} {da-db:7.1f}")
