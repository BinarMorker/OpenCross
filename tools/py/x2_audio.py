"""PS1 (ps1_audio.py audio.raw / audio.wav, raw s16 stereo 44.1k) analysis helpers. usage: x2_audio.py raw [t0 t1]"""
import sys, numpy as np, wave
def load(p):
    a = np.fromfile(p, dtype='<i2').reshape(-1, 2).astype(np.float64) / 32768.0
    return a
def wav(p, a, sr=44100):
    with wave.open(p, 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes((np.clip(a, -1, 1) * 32767).astype('<i2').tobytes())
def load_wav(p):
    with wave.open(p) as w:
        sr = w.getframerate(); n = w.getnchannels()
        d = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64) / 32768
    d = d.reshape(-1, n)
    if n == 1: d = np.repeat(d, 2, 1)
    return d, sr
def events(a, sr=44100, thr=0.003, gap=0.15):
    m = np.abs(a).max(1); w = int(sr * 0.01)
    e = np.sqrt(np.convolve(m * m, np.ones(w) / w, 'same'))
    on = e > thr; res = []; i = 0; n = len(on)
    while i < n:
        if on[i]:
            j = i
            last = i
            while j < n and j - last < gap * sr:
                if on[j]: last = j
                j += 1
            res.append((i / sr, last / sr)); i = j
        else: i += 1
    return res
def stats(a, sr=44100):
    m = a.mean(1)
    if len(m) < 64: return {}
    n = 1 << int(np.ceil(np.log2(len(m))))
    S = np.abs(np.fft.rfft(m * np.hanning(len(m)), n)); f = np.fft.rfftfreq(n, 1 / sr)
    cen = (S * f).sum() / max(S.sum(), 1e-9)
    pk = f[1:][S[1:].argmax()]
    rms = np.sqrt((m ** 2).mean()); pkv = np.abs(a).max()
    l = np.sqrt((a[:, 0] ** 2).mean()); r = np.sqrt((a[:, 1] ** 2).mean())
    return dict(dur=len(m) / sr, rms=round(rms, 4), peak=round(pkv, 3), centroid=round(cen), fpeak=round(pk, 1), L=round(l, 4), R=round(r, 4))
if __name__ == '__main__':
    a = load(sys.argv[1])
    print(len(a) / 44100, 's')
    for s, e in events(a):
        print(f"{s:7.3f} - {e:7.3f}  frame~{s*60:.0f}", stats(a[int(s*44100):int(e*44100)+1]))
