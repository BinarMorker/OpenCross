"""PS1 recording vs our render for a list of (raw, t0, dur, wav) -> one comparison line each.
usage: x2_table.py raw:t0:dur:ours.wav ..."""
import sys; sys.path.insert(0, __import__('os').path.dirname(__file__))
from x2_audio import *
sr = 44100
def xc(x, y):
    n = 1 << int(np.ceil(np.log2(len(x) + len(y))))
    cc = np.fft.irfft(np.fft.rfft(x, n) * np.conj(np.fft.rfft(y, n)), n)
    lags = np.concatenate([cc[-(len(y)-1):], cc[:len(x)]])
    return int(np.argmax(lags)) - (len(y) - 1)
def cent(a):
    m = a.mean(1); n = 1 << 16
    S = np.abs(np.fft.rfft(m * np.hanning(len(m)), n)); f = np.fft.rfftfreq(n, 1/sr)
    return (S * f).sum() / S.sum()
for arg in sys.argv[1:]:
    raw, t0, dur, wav = arg.split(':'); t0 = float(t0); dur = float(dur)
    a = load(raw); b, _ = load_wav(wav)
    x = a[int(t0*sr):int((t0+dur+0.2)*sr)]; y = b[:int((dur+0.2)*sr)]
    lag = xc(x[:, 0], y[:, 0])
    xs = x[max(0, lag):][:int(dur*sr)]; ys = y[max(0, -lag):][:len(xs)]
    m = min(len(xs), len(ys)); xs, ys = xs[:m], ys[:m]
    corr = float(np.dot(xs[:, 0], ys[:, 0]) / (np.linalg.norm(xs[:, 0]) * np.linalg.norm(ys[:, 0]) + 1e-12))
    r = lambda z: float(np.sqrt((z ** 2).mean()))
    print(f"{wav.split('/')[-1]:22s} lag {lag/sr*1000:6.1f}ms corr {corr:5.3f} | rms ps1 {r(xs):.4f} ours {r(ys):.4f} ratio {r(xs)/max(r(ys),1e-9):.2f} | peak {np.abs(xs).max():.3f}/{np.abs(ys).max():.3f} | centroid {cent(xs):.0f}/{cent(ys):.0f} | L/R ps1 {r(xs[:,0])/max(r(xs[:,1]),1e-9):.2f} ours {r(ys[:,0])/max(r(ys[:,1]),1e-9):.2f}")
