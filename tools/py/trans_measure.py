"""Per-frame transition measurements for ccx / OpenCross frame dirs (numpy + PIL; run with python -I).

    python -I tools/py/trans_measure.py DIR [step] [cx cy]     one line per frame: n  mean-luma  p5 p50 p95  zoom  white%  dark%
Frames are cropped to the picture if letterboxed (ccx 640x448 as is; ours 1280x960 with 8px bands is cropped by the caller's
--crop W H x y). zoom = the scale s (about cx,cy given in 0..1 of the picture, default .5 .625) that best maps frame n-1 onto n
(least squares on a 160x112 luma copy; 1.000 = none)."""
import sys, os, glob
import numpy as np
from PIL import Image


def load(path, crop=None):
    im = Image.open(path).convert('L')
    if crop:
        x, y, w, h = crop
        im = im.crop((x, y, x + w, y + h))
    return np.asarray(im.resize((160, 112), Image.BILINEAR), dtype=np.float32)


def zoomed(a, s, cx, cy):
    h, w = a.shape
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
    sx = cx + (xs - cx) / s
    sy = cy + (ys - cy) / s
    x0 = np.clip(sx.astype(int), 0, w - 2); y0 = np.clip(sy.astype(int), 0, h - 2)
    fx = np.clip(sx - x0, 0, 1); fy = np.clip(sy - y0, 0, 1)
    return (a[y0, x0] * (1 - fx) * (1 - fy) + a[y0, x0 + 1] * fx * (1 - fy) + a[y0 + 1, x0] * (1 - fx) * fy + a[y0 + 1, x0 + 1] * fx * fy)


def best_zoom(prev, cur, cx, cy):
    if cur.std() < 2 or prev.std() < 2:
        return 0.0
    best, bs = 1e18, 1.0
    for s in np.arange(0.96, 1.2001, 0.004):
        e = float(((zoomed(prev, s, cx, cy) - cur) ** 2).mean())
        if e < best:
            best, bs = e, s
    return bs


def main(argv):
    d = argv[1]
    step = int(argv[2]) if len(argv) > 2 else 1
    cx = (float(argv[3]) if len(argv) > 3 else 0.5) * 160
    cy = (float(argv[4]) if len(argv) > 4 else 0.625) * 112
    crop = None
    if os.environ.get('CROP'):
        crop = tuple(int(v) for v in os.environ['CROP'].split(','))
    prev = None
    for f in sorted(glob.glob(os.path.join(d, 'frame_*.png'))):
        n = int(os.path.basename(f)[6:12])
        if n % step:
            continue
        a = load(f, crop)
        z = best_zoom(prev, a, cx, cy) if prev is not None else 0.0
        p = np.percentile(a, [5, 50, 95])
        print(f'{n:4d} L{a.mean():6.1f}  p5 {p[0]:5.1f} p50 {p[1]:5.1f} p95 {p[2]:5.1f}  z {z:5.3f}  white {100*(a>240).mean():5.1f}%  dark {100*(a<16).mean():5.1f}%')
        prev = a


main(sys.argv)
