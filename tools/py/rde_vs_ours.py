"""Ours (route enc_k per-tick shots tests/routes/shots/ek_NNN.png, 1280x960 with 8 px bands) vs RDE (tools/rde_capture.py frames):
python tools/py/rde_vs_ours.py RDEDIR [RDEDIR2..] > table.  Both aligned on the swirl's first white frame (t = 0); L = PIL luma of the
picture; gy = mean abs vertical gradient at 320x240.  RDE times are real ms; ours tick * 16.667."""
import sys, os, glob
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
def stats(im):
    g = np.asarray(im.convert('L').resize((320, 240), Image.BOX), dtype=np.float32)
    return float(g.mean()), float(np.abs(np.diff(g, axis=0)).mean())
def ours(pattern='tests/routes/shots/ek_*.png'):
    out = []
    for f in sorted(glob.glob(pattern)):
        n = int(os.path.basename(f)[3:-4]); L, gy = stats(Image.open(f).crop((0, 8, 1280, 952))); out.append((n * 1000 / 60.0, L, gy))
    return out
def rde(d):
    out = []
    for f in sorted(os.listdir(d)):
        t = int(f.split('_')[1]); L, gy = stats(Image.open(os.path.join(d, f)).crop((0, 32, 1280, 992))); out.append((t, L, gy))
    return out
def first_white(s, lo=0):
    for t, L, g in s:
        if t >= lo and L > 200: return t
if __name__ == '__main__':
    o = ours(); tw_o = first_white(o); print('ours white at', tw_o)
    for d in sys.argv[1:]:
        r = rde(d)
        # the encounter's white is the last one (the results exit has an earlier fade-to-white)
        ws = [t for t, L, g in r if L > 200]; tw_r = [w for w in ws if w >= ws[-1] - 400][0] if ws else None
        print('RDE', d, 'white at', tw_r)
    # side by side at 100 ms steps: mean L of the frames in each bin
    def curve(s, t0):
        b = {}
        for t, L, g in s: b.setdefault(int(round((t - t0) / 100.0)), []).append((L, g))
        return {k: (np.mean([x[0] for x in v]), np.mean([x[1] for x in v])) for k, v in b.items()}
    co = curve(o, tw_o)
    cr = []
    for d in sys.argv[1:]:
        r = rde(d); ws = [t for t, L, g in r if L > 200]
        if ws: cr.append(curve(r, [w for w in ws if w >= ws[-1] - 400][0]))
    print(' t_ms |  ours L  gy | ' + ' | '.join(f'RDE{i} L  gy' for i in range(len(cr))))
    for k in range(-15, 45):
        a = co.get(k); row = f'{k*100:5d} | ' + (f'{a[0]:6.1f} {a[1]:5.2f}' if a else '   -      -') + ' | '
        row += ' | '.join((f'{c[k][0]:6.1f} {c[k][1]:5.2f}' if k in c else '   -      -') for c in cr)
        print(row)
