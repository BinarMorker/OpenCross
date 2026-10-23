"""First sample where ours diverges from the PS1 recording. usage: x2_div.py ps1.raw t0 ours.wav ps1_onset_sample our_onset_sample"""
import sys; sys.path.insert(0, __import__('os').path.dirname(__file__))
from x2_audio import *
a = load(sys.argv[1]); sr = 44100; t0 = float(sys.argv[2])
b, _ = load_wav(sys.argv[3])
def on(x): return int(np.argmax(np.abs(x).max(1) > 0.0005))
po = on(a[int(t0*sr):int((t0+0.3)*sr)]) + int(t0*sr)
bo = on(b)
n = min(len(a) - po, len(b) - bo, 30000)
d = np.abs(a[po:po+n, 0] - b[bo:bo+n, 0])
bad = np.where(d > 0.002)[0]
print('ps1 onset', po, 'our onset', bo, 'first diverge at sample', bad[0] if len(bad) else None, f'({bad[0]/sr*1000:.1f} ms)' if len(bad) else '')
for k in (200, 500, 1000, 2000, 5000, 10000, 20000):
    if k < n:
        x = a[po+k-100:po+k+100, 0]; y = b[bo+k-100:bo+k+100, 0]
        print(k, 'rms ps1 %.4f ours %.4f diff %.4f' % (np.sqrt((x**2).mean()), np.sqrt((y**2).mean()), np.sqrt(((x-y)**2).mean())))
