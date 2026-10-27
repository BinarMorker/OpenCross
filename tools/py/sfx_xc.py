"""Waveform cross-correlation PS1 segment vs ours. usage: sfx_xc.py raw t_event ours.wav t0_ms t1_ms"""
import sys, os, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from x2_audio import load, load_wav
raw, t, w = sys.argv[1], float(sys.argv[2]), sys.argv[3]
t0, t1 = float(sys.argv[4]) / 1000, float(sys.argv[5]) / 1000
p = load(raw); o, _ = load_wav(w)
best = (-2, 0)
for lag in range(-1500, 1500):  # PS1 offset samples
    a = p[int((t + t0) * 44100) + lag:int((t + t1) * 44100) + lag].mean(1)
    b = o[int(t0 * 44100):int(t1 * 44100)].mean(1)
    if len(a) != len(b): continue
    c = np.corrcoef(a, b)[0, 1]
    if c > best[0]: best = (c, lag)
print("best corr %.3f at lag %d samples (%.2f ms); gain ps1/ours %.3f" % (best[0], best[1], best[1] / 44.1,
      np.sqrt((p[int((t+t0)*44100)+best[1]:int((t+t1)*44100)+best[1]].mean(1)**2).mean() / (o[int(t0*44100):int(t1*44100)].mean(1)**2).mean())))
