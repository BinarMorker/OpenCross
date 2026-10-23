"""FFT cross-correlation alignment + per-block gain/corr. usage: x2_xc.py ps1.raw t0 dur ours.wav [step_ms]"""
import sys; sys.path.insert(0, __import__('os').path.dirname(__file__))
from x2_audio import *
a = load(sys.argv[1]); sr = 44100; t0 = float(sys.argv[2]); dur = float(sys.argv[3])
b, _ = load_wav(sys.argv[4]); step = float(sys.argv[5]) / 1000 if len(sys.argv) > 5 else 0.05
x = a[int(t0*sr):int((t0+dur+0.5)*sr), 0]; y = b[:int((dur+0.5)*sr), 0]
n = 1 << int(np.ceil(np.log2(len(x) + len(y))))
X = np.fft.rfft(x, n); Y = np.fft.rfft(y, n)
cc = np.fft.irfft(X * np.conj(Y), n)
lags = np.concatenate([cc[-(len(y)-1):], cc[:len(x)]])  # lag = -(len(y)-1) .. len(x)-1
k = int(np.argmax(lags)); lag = k - (len(y) - 1)
print('best lag (ps1 later than ours by)', lag, f'{lag/sr*1000:.2f} ms')
xs = x[max(0, lag):]; ys = y[max(0, -lag):]
m = min(len(xs), len(ys), int(dur*sr)); xs = xs[:m]; ys = ys[:m]
g = np.dot(xs, ys) / np.dot(ys, ys)
print('global corr %.3f gain %.3f' % (np.dot(xs, ys)/np.linalg.norm(xs)/np.linalg.norm(ys), g))
w = int(step*sr)
for i in range(0, m - w, w):
    p, q = xs[i:i+w], ys[i:i+w]
    print(f'{i/sr:5.2f} ps1 {np.sqrt((p**2).mean())*1000:7.1f} ours {np.sqrt((q**2).mean())*1000:7.1f}  corr {np.dot(p,q)/(np.linalg.norm(p)*np.linalg.norm(q)+1e-12):6.3f}')
