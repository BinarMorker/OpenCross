"""Compare a ccx reference capture with our ocbattleview shots by region statistics (no image viewing).
usage: cmp_frames.py REFDIR OURSDIR OFFSET FIRST LAST STEP [COLS ROWS]
  REFDIR/frames/frame_NNNNNN.png (640x432)  vs  OURSDIR/shot_{N+OFFSET:06d}.png (1280x960, picture rows 8..951)
prints per frame: mean luma R G B (ref | ours) and a COLSxROWS luma grid of both, plus mean |grid diff|."""
import sys, os
import numpy as np
from PIL import Image

# the HD HUD (banner top-left, party panels bottom) is absent in the ccx frames: compare rows 10 %..74 % only (CROP=y0,y1 fractions)
Y0, Y1 = (float(v) for v in os.environ.get('CROP', '0.10,0.74').split(','))

def load(p, ours):
    im = Image.open(p).convert('RGB')
    if ours:
        im = im.crop((0, 8, im.width, im.height - 8))
    a = np.asarray(im.resize((320, 216), Image.BOX), dtype=np.float32)
    return a[int(216 * Y0):int(216 * Y1)]

def luma(a): return a[..., 0] * .299 + a[..., 1] * .587 + a[..., 2] * .114

def grid(l, c, r):
    h, w = l.shape
    return np.array([[l[y * h // r:(y + 1) * h // r, x * w // c:(x + 1) * w // c].mean() for x in range(c)] for y in range(r)])

ref, ours, off, f0, f1, st = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
c, r = (int(sys.argv[7]), int(sys.argv[8])) if len(sys.argv) > 8 else (4, 3)
for f in range(f0, f1 + 1, st):
    pr = os.path.join(ref, 'frames', 'frame_%06d.png' % f)
    po = os.path.join(ours, 'shot_%06d.png' % (f + off))
    if not (os.path.exists(pr) and os.path.exists(po)): continue
    a, b = load(pr, False), load(po, True)
    la, lb = luma(a), luma(b)
    ga, gb = grid(la, c, r), grid(lb, c, r)
    fa = ' '.join('%3d' % v for v in ga.flatten()); fb = ' '.join('%3d' % v for v in gb.flatten())
    print('%4d L %5.1f|%5.1f rgb %3d %3d %3d|%3d %3d %3d  grid %s | %s  d%5.1f' % (f, la.mean(), lb.mean(), *a.reshape(-1, 3).mean(0), *b.reshape(-1, 3).mean(0), fa, fb, abs(ga - gb).mean()))
