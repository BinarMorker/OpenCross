"""PS1 recording segment vs our offline render of one battle sound.
usage: sfx_wave_cmp.py <audio.raw> <t_event_s> <ours.wav> [dur_s=1.0] [win_ms=20]
The PS1 mix contains the AKAO battle music; the 'bg' rms (0.3 s before the event) shows how much. Prints per-window rms and spectral
centroid for both, the L/R ratio, and the best lag of the envelope cross-correlation (PS1 starts ~2 SFX ticks = 21 ms after the request)."""
import sys, numpy as np
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from x2_audio import load, load_wav

def frames(a, n):
    k = len(a) // n
    return a[:k * n].reshape(k, n, -1)

def rms(a, n): return np.sqrt((frames(a, n) ** 2).mean(axis=(1, 2)))

def cent(a, n, sr=44100):
    f = np.fft.rfftfreq(n, 1 / sr); out = []
    for fr in frames(a, n).mean(axis=2):
        S = np.abs(np.fft.rfft(fr * np.hanning(n)))
        out.append((S * f).sum() / S.sum() if S.sum() > 1e-6 else 0)
    return np.array(out)

if __name__ == '__main__':
    raw, t = sys.argv[1], float(sys.argv[2]); ours = sys.argv[3]
    dur = float(sys.argv[4]) if len(sys.argv) > 4 else 1.0
    wms = float(sys.argv[5]) if len(sys.argv) > 5 else 20.0
    n = int(44100 * wms / 1000)
    p = load(raw); o, sr = load_wav(ours)
    bg = p[int((t - 0.3) * 44100):int(t * 44100)]
    ps = p[int(t * 44100):int((t + dur) * 44100)]
    os_ = o[:int(dur * 44100)]
    print(f"bg rms {np.sqrt((bg**2).mean()):.4f}  ps1 rms {np.sqrt((ps**2).mean()):.4f}  ours rms {np.sqrt((os_**2).mean()):.4f}  "
          f"L/R ps1 {np.sqrt((ps[:,0]**2).mean())/max(np.sqrt((ps[:,1]**2).mean()),1e-9):.2f} ours {np.sqrt((os_[:,0]**2).mean())/max(np.sqrt((os_[:,1]**2).mean()),1e-9):.2f}")
    rp, ro = rms(ps, n), rms(os_, n); cp, co = cent(ps, n), cent(os_, n)
    m = min(len(rp), len(ro))
    lag = 0; best = -2
    for l in range(-5, 12):
        a = rp[max(l, 0):m]; b = ro[max(-l, 0):m - max(l, 0) + max(-l, 0)]
        k = min(len(a), len(b))
        if k > 3:
            c = np.corrcoef(a[:k], b[:k])[0, 1]
            if c > best: best, lag = c, l
    print(f"envelope corr {best:.3f} at ps1 lag {lag*wms:.0f} ms")
    print("  t_ms   rms_ps1  rms_ours   cen_ps1  cen_ours")
    for i in range(m):
        print(f"{i*wms:6.0f}  {rp[i]:8.4f} {ro[i]:8.4f}  {cp[i]:8.0f} {co[i]:8.0f}")
