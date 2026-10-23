"""Offline simulation of the PS1 strip feed-back against the RDE capture (frames2).
out(u) = 0.5 * B + 0.5 * g * Z(out(u-1); s_u) per 15 Hz update, B = the pre-white picture P (the scene behind the strips)."""
import site, sys, os, glob, re
sys.path.append(site.getusersitepackages())
import numpy as np, cv2
D = 'tmp/rde_trans/frames2'
W0 = 18306  # white frame ms
fl = sorted((int(re.search(r'f_(\d+)_', p).group(1)), p) for p in glob.glob(D + '/f_*.png'))
def rde(ms):
    t, p = min(fl, key=lambda e: abs(e[0] - ms))
    im = cv2.imread(p)[32:992]
    return cv2.resize(im, (320, 240), interpolation=cv2.INTER_AREA).astype(np.float32) / 255
def Z(a, s, cx, cy):
    M = np.array([[s, 0, cx * (1 - s)], [0, s, cy * (1 - s)]], np.float32)
    return cv2.warpAffine(a, M, (320, 240), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT, borderValue=(0, 0, 0))
def run(sched, g=1.0, cx=160, cy=150, n=40, keepB=None):
    P = rde(18267)
    white = np.ones_like(P) * 0.93
    out = white
    res = []
    for u in range(1, n):
        s = sched(u)
        out = 0.5 * P + 0.5 * np.minimum(g * Z(out, s, cx, cy), 1.5)
        out = np.clip(out, 0, 1)
        res.append(out)
    return res
def score(res, ts):
    e = []
    for r, t in zip(res, ts):
        a = rde(t)
        e.append(np.abs(r - a).mean())
    return np.array(e)
if __name__ == '__main__':
    # update times (ms) of RDE: white + 4 gf each
    ts = [W0 + 66.7 * u for u in range(1, 40)]
    for a in (0, 20, 40, 80, 160):
        for cx, cy in ((160, 150), (152, 126), (160, 120)):
            sc = lambda u, a=a: 4096.0 / (4096 - a * u)
            e = score(run(sc, 1.0, cx, cy, 14), ts[:13])
            print('a', a, 'c', cx, cy, 'err mean %.4f' % e.mean(), ' '.join('%.3f' % v for v in e[::3]))
