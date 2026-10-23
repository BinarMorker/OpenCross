"""Matched-phase montages RDE vs ours, whole frames and tight crops.
python -I tools/py/t_mont.py rde DIR WHITE_MS OUT.png GF... [--crop x0,y0,w,h]   (RDE: frame nearest WHITE_MS + gf*16.667; crop in 1280x960 pixel coords)
python -I tools/py/t_mont.py ours PREFIX FIRST OUT.png GF... [--crop ...]       (ours: shot PREFIX_NNN, NNN = FIRST + gf, 1280x960)
Cells are 640 wide (whole) or the crop; labelled gf + mean luma. RDE sources are 1280x1024 with bars (rows 32:992 kept)."""
import site, sys, os, glob, re
sys.path.append(site.getusersitepackages())
import cv2, numpy as np
a = sys.argv[1:]
mode, src, ref, out = a[0], a[1], a[2], a[3]
rest = a[4:]
crop = None
if '--crop' in rest:
    i = rest.index('--crop'); crop = [int(v) for v in rest[i + 1].split(',')]; rest = rest[:i] + rest[i + 2:]
gfs = [int(v) for v in rest]
def load_rde(gf):
    fl = [(int(re.search(r'f_(\d+)_', p).group(1)), p) for p in glob.glob(src + '/f_*.png')]
    t = float(ref) + gf * 1000 / 60
    tt, p = min(fl, key=lambda e: abs(e[0] - t))
    im = cv2.imread(p)
    if im.shape[0] == 1024: im = im[32:992]
    return im
def load_ours(gf):
    p = '%s_%03d.png' % (src, int(ref) + gf)
    im = cv2.imread(p)
    if im is None: return np.zeros((960, 1280, 3), np.uint8)
    return im
tiles = []
for gf in gfs:
    im = load_rde(gf) if mode == 'rde' else load_ours(gf)
    im = cv2.resize(im, (1280, 960), interpolation=cv2.INTER_AREA) if im.shape[:2] != (960, 1280) else im
    if crop:
        x, y, w, h = crop; c = im[y:y + h, x:x + w]
    else:
        c = cv2.resize(im, (640, 480), interpolation=cv2.INTER_AREA)
    c = c.copy()
    cv2.putText(c, 'gf%d L%.0f' % (gf, c.mean()), (4, 16), 0, .55, (0, 255, 255), 1)
    tiles.append(c)
cols = 3 if not crop else min(len(tiles), 5)
while len(tiles) % cols: tiles.append(np.zeros_like(tiles[0]))
cv2.imwrite(out, np.vstack([np.hstack(tiles[i:i + cols]) for i in range(0, len(tiles), cols)]))
