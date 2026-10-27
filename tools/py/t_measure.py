"""Structure measures of the transition frames, RDE vs ours at matched phases.
python tools/py/t_measure.py DIR_RDE WHITE_MS PREFIX_OURS FIRST GF...
 per phase: L, outer-10 % band L (live scene vs copies), centre/band ratio, mean gradient (sharpness), radial luminance autocorrelation peak (lag in
 320x240 px; radial streak copies repeat with the zoom step), 50 % blend check: contrast ratio of consecutive distinct pictures."""
import site, sys, glob, re
sys.path.append(site.getusersitepackages())
import numpy as np, cv2
d, w0, pre, first = sys.argv[1], float(sys.argv[2]), sys.argv[3], int(sys.argv[4])
gfs = [int(v) for v in sys.argv[5:]]
fl = [(int(re.search(r'f_(\d+)_', p).group(1)), p) for p in glob.glob(d + '/f_*.png')]
def rde(gf):
    t = w0 + gf * 1000 / 60
    p = min(fl, key=lambda e: abs(e[0] - t))[1]
    im = cv2.imread(p)
    if im.shape[0] == 1024: im = im[32:992]
    return cv2.cvtColor(cv2.resize(im, (320, 240), interpolation=cv2.INTER_AREA), cv2.COLOR_BGR2GRAY).astype(np.float32)
def ours(gf):
    im = cv2.imread('%s_%03d.png' % (pre, first + gf))
    return cv2.cvtColor(cv2.resize(im, (320, 240), interpolation=cv2.INTER_AREA), cv2.COLOR_BGR2GRAY).astype(np.float32)
def stats(g):
    h, w = g.shape
    m = np.zeros_like(g, bool); b = int(0.1 * w); bv = int(0.1 * h)
    m[:bv] = m[-bv:] = True; m[:, :b] = m[:, -b:] = True
    band = g[m].mean(); cen = g[~m].mean()
    gx = np.abs(np.diff(g, axis=1)).mean()
    # radial profile autocorrelation of |gradient| along rays from (160, 120)
    gm = cv2.GaussianBlur(g, (0, 0), 1.0)
    gmag = np.hypot(cv2.Sobel(gm, cv2.CV_32F, 1, 0), cv2.Sobel(gm, cv2.CV_32F, 0, 1))
    ac = np.zeros(60)
    for a in np.linspace(0, 2 * np.pi, 48, endpoint=False):
        rr = np.arange(20, 140)
        xs = 160 + rr * np.cos(a); ys = 120 + rr * 0.8 * np.sin(a)
        ok = (xs > 0) & (xs < 319) & (ys > 0) & (ys < 239)
        p = gmag[ys[ok].astype(int), xs[ok].astype(int)]
        if len(p) < 80: continue
        p = p - p.mean()
        for lag in range(60): ac[lag] += (p[:len(p) - lag] * p[lag:]).mean() / (p.var() + 1e-6)
    ac /= ac[0]
    pk = 5 + int(np.argmax(ac[5:40]))
    return g.mean(), band, cen / max(band, 1e-3), gx, pk, ac[pk]
print('gf   src    L    band   cen/band  grad  acPeakLag acPeak')
for gf in gfs:
    for name, f in (('RDE', rde), ('ours', ours)):
        try:
            s = stats(f(gf))
            print('%-4d %-5s %5.1f %6.1f %6.2f   %5.2f  %3d   %.2f' % (gf, name, *s))
        except Exception as e:
            print(gf, name, 'n/a', e)
