"""Per-frame radial zoom of the field -> battle transition smear.  python -I tools/py/zoom_track.py ARGS

    python tools/py/zoom_track.py DIR[#prefix] FIRST_MS LAST_MS [--cx X --cy Y] [--fps60]
        DIR holds RDE frames f_<ms>_*.png (real ms stamps) or our route shots <prefix>_NNN.png (one per 60 Hz tick).
        For each consecutive pair (a, b) estimates the scale s that best maps a onto b about the centre
        (b(c + s (p - c)) ~ a(p) - the content moves OUTWARD when s > 1), by a 1-D search of s over 0.90..1.30 with
        the centre fixed, then refines the centre on a coarse grid at the best s.  The score is the normalised
        cross-correlation of the high-passed pictures, so the flat brightness change of a fade does not matter.
        Prints per pair: time, dt in 60 Hz game frames, L (mean luma), s, s per game frame (s ** (1 / dt)), centre,
        correlation at s and at s = 1 (a high 'c1' with a low 'cs' gain = no motion), sharpness g.
    First/last are ms for RDE dirs (frame f_<ms>) or tick numbers for ours.
Under python -I the user site is added for numpy / cv2 / PIL.
"""
import site, sys, os, re, glob
sys.path.append(site.getusersitepackages())
import numpy as np
import cv2


def load(path):
    im = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
    h = im.shape[0]
    if h == 1024:
        im = im[32:992]
    elif h == 960:
        im = im[8:952] if False else im          # ours is 1280x960 without bars
    im = cv2.resize(im, (320, 240), interpolation=cv2.INTER_AREA)
    return im.astype(np.float32)


def hp(a):
    return a - cv2.GaussianBlur(a, (0, 0), 6)


def warp(a, s, cx, cy):
    M = np.array([[s, 0, cx * (1 - s)], [0, s, cy * (1 - s)]], np.float32)
    return cv2.warpAffine(a, M, (a.shape[1], a.shape[0]), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_REFLECT)


def ncc(x, y):
    x = x - x.mean(); y = y - y.mean()
    d = np.sqrt((x * x).sum() * (y * y).sum()) + 1e-6
    return float((x * y).sum() / d)


def fit(a, b, cx=160.0, cy=150.0, refine=True):
    ha, hb = hp(a), hp(b)
    best = (-2, 1.0)
    for s in np.arange(0.90, 1.30001, 0.005):
        # b(p) vs a at the inverse mapping: warp a outward by s
        c = ncc(warp(ha, s, cx, cy), hb)
        if c > best[0]:
            best = (c, s)
    c, s = best
    if refine:
        bc = (c, cx, cy)
        for dx in (-60, -30, 0, 30, 60):
            for dy in (-60, -30, 0, 30, 60):
                for ss in (s - 0.01, s, s + 0.01):
                    cc = ncc(warp(ha, ss, cx + dx, cy + dy), hb)
                    if cc > bc[0] + 1e-4:
                        bc = (cc, cx + dx, cy + dy); s = ss
        c, cx, cy = bc
    return s, cx, cy, c, ncc(ha, hb)


def sharp(a):
    return float(np.abs(np.diff(a, axis=1)).mean())


def listing(spec):
    d, _, pre = spec.partition('#')
    pre = pre.rstrip('_')
    out = []
    for p in glob.glob(os.path.join(d, '*.png')):
        n = os.path.basename(p)
        m = re.match(r'f_(\d+)_', n)
        if m and not pre:
            out.append((int(m.group(1)), p))
            continue
        m = re.match(r'(.+?)_(\d+)\.png$', n)
        if m and pre and m.group(1) == pre:
            out.append((int(m.group(2)), p))
    out.sort()
    return out, (not pre)


def diff_mode(L, rde, cx, cy):
    # distinct frames only; d_k = f_k - f_(k-1) propagates as d_k ~ 0.5 Z_s(d_(k-1)) when the scene is constant,
    # so the scale between consecutive differences is the feed-back zoom (the static scene cancels).
    fr = []
    for t, p in L:
        im = load(p)
        if fr and np.abs(im - fr[-1][1]).mean() < 0.08:
            continue
        fr.append((t, im))
    print('t        dt60   L      s      s/gf    cx   cy    cs    |d|   (diff mode, %d distinct frames)' % len(fr))
    pd = None
    for i in range(1, len(fr)):
        t, im = fr[i]; pt, pim = fr[i - 1]
        d = im - pim
        if pd is not None and np.abs(d).mean() > 0.15 and np.abs(pd[1]).mean() > 0.15:
            dt = (t - pt) * 60 / 1000.0 if rde else float(t - pt)
            s, x, y, c, _ = fit(pd[1], d, cx, cy)
            print('%-8d %5.1f %6.1f %.3f  %.4f  %4.0f %4.0f  %.2f  %.2f' % (t, dt, im.mean(), s, s ** (1 / max(dt, 0.5)), x, y, c, np.abs(d).mean()))
        pd = (pt, d)


def main():
    a = sys.argv[1:]
    spec, lo, hi = a[0], float(a[1]), float(a[2])
    cx = float(a[a.index('--cx') + 1]) if '--cx' in a else 160.0
    cy = float(a[a.index('--cy') + 1]) if '--cy' in a else 150.0
    L, rde = listing(spec)
    L = [(t, p) for t, p in L if lo <= t <= hi]
    if '--diff' in a:
        return diff_mode(L, rde, cx, cy)
    prev = None
    print('t        dt60   L      s      s/gf    cx   cy    cs    c1    g')
    for t, p in L:
        im = load(p)
        if prev is not None:
            pt, pim = prev
            dt = (t - pt) * 60 / 1000.0 if rde else float(t - pt)
            s, x, y, c, c1 = fit(pim, im, cx, cy)
            print('%-8d %5.1f %6.1f %.3f  %.4f  %4.0f %4.0f  %.2f  %.2f  %.2f' % (
                t, dt, im.mean(), s, s ** (1 / max(dt, 0.5)), x, y, c, c1, sharp(im)))
        prev = (t, im)


if __name__ == '__main__':
    main()
