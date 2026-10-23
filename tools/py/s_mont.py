"""Montage of the frames nearest to given times.  python -I tools/py/s_mont.py DIR[#prefix] OUT.png T0 T1 STEP [COLS]
RDE dirs: T in ms (f_<ms>); ours: T in ticks.  Cells 320x240 labelled with the time and mean luma."""
import site, sys, os
sys.path.append(site.getusersitepackages())
import cv2, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from zoom_track import listing
spec, out = sys.argv[1], sys.argv[2]
t0, t1, st = float(sys.argv[3]), float(sys.argv[4]), float(sys.argv[5])
cols = int(sys.argv[6]) if len(sys.argv) > 6 else 4
L, _ = listing(spec)
tiles = []
t = t0
while t <= t1:
    tt, p = min(L, key=lambda e: abs(e[0] - t))
    im = cv2.imread(p)
    if im.shape[0] == 1024:
        im = im[32:992]
    im = cv2.resize(im, (320, 240), interpolation=cv2.INTER_AREA)
    cv2.putText(im, '%d L%.0f' % (tt, im.mean()), (4, 14), 0, .45, (0, 255, 255), 1)
    tiles.append(im)
    t += st
while len(tiles) % cols:
    tiles.append(np.zeros_like(tiles[0]))
cv2.imwrite(out, np.vstack([np.hstack(tiles[i:i + cols]) for i in range(0, len(tiles), cols)]))
