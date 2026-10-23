"""Sample-level alignment. usage: x2_wave.py ps1.raw t0 dur ours.wav"""
import sys; sys.path.insert(0, __import__('os').path.dirname(__file__))
from x2_audio import *
a = load(sys.argv[1]); sr = 44100; t0 = float(sys.argv[2]); dur = float(sys.argv[3])
ps = a[int(t0*sr):int((t0+dur)*sr)].mean(1)
b, _ = load_wav(sys.argv[4]); b = b.mean(1)[:int(dur*sr)+4000]
# search lag
best = (0, -1)
for lag in range(-300, 3000):
    x = ps[max(0, lag):]; y = b[max(0, -lag):]
    n = min(len(x), len(y), int(0.25*sr))
    if n < 1000: continue
    c = np.dot(x[:n], y[:n]) / (np.linalg.norm(x[:n]) * np.linalg.norm(y[:n]) + 1e-12)
    if c > best[1]: best = (lag, c)
lag, c = best
x = ps[max(0, lag):]; y = b[max(0, -lag):]
n = min(len(x), len(y), int(dur*sr))
g = np.dot(x[:n], y[:n]) / np.dot(y[:n], y[:n])
print('lag', lag, f'({lag/sr*1000:.2f} ms: PS1 later by that)', 'corr %.3f' % c, 'gain ps1/our %.3f' % g)
w = int(0.02*sr)
print('20ms blocks  ps1rms / ourrms / corr:')
for i in range(0, min(n, int(dur*sr)) - w, w):
    xs, ys = x[i:i+w], y[i:i+w]
    print(f'{i/sr:5.2f} {np.sqrt((xs**2).mean())*1000:7.1f} {np.sqrt((ys**2).mean())*1000:7.1f}  {np.dot(xs,ys)/(np.linalg.norm(xs)*np.linalg.norm(ys)+1e-12):6.3f}')
