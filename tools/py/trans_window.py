"""Field -> battle transition window finder / comparer (numpy + PIL; run as  python tools/py/trans_window.py ...).

One tool so that nobody captures or reads frames that are not part of the transition.

    python tools/py/trans_window.py segment DIR
        Finds the window in a frame directory and prints one line per phase (frame name, time, game frame relative to the
        white clear, mean luma L, sharpness g, blue mask fraction b).  Frames outside the window are ignored.
    python tools/py/trans_window.py table RDE_DIR CCX_DIR OURS_DIR [--step N]
        Aligns the three sources on the first white frame (the swirl's white clear), prints a per-phase table (duration in game
        frames of 1/60 s, L and sharpness at phase start/end, smear slope) and a luma table every N game frames (default 10)
        with the deltas.  A source may be '-' to leave it out.
    python tools/py/trans_window.py rings FRAME.png [--centre cx cy]
        Ring-ness of the contact effect (blue mask): radial profile about the mask centroid and about the screen centre,
        peaks of the radial profile (1 = one smooth ring), radius / width, and the angular coefficient of variation of the
        blue coverage on the ring (small = smooth ring, large = shattered wedges).

Sources are detected from the file names:  f_<ms>_*.png = RDE Steam capture (tools/rde_capture.py, real time stamps, ~15-30 fps),
frame_NNNNNN.png = ccx (60 fps, run frame; ccx tick = run + 9), <prefix>_NNN.png = our route autoshots (every tick, 60 fps).
Letterboxes are cropped by image height (1024 -> 32..992 RDE, 960 -> 8..952 ours).  Missing frames are interpolated by time
stamp, so a ~30 fps RDE capture lines up with the 60 fps sources.  Per-frame stats are cached in DIR/trans_stats.json
(delete it to force a re-read).  A shots directory with several prefixes (es_, ek_, ...) uses the biggest group, or name one:
DIR#ek_ (also for table's third argument); a cold 567-frame RDE directory takes ~3 s, a warm run < 0.5 s.

Phases (all derived from luma L, blue-ring mask b, white fraction and the mean abs gradient g of a 160x112 copy):
    contact   first frame of the blue ring cluster (b > 0.01; clusters closer than 12 game frames merge)
    ring_peak frame of the largest b
    swirl     first frame after the ring cluster (the radial smear / zoom runs on its own)
    white     first flat white frame (>90 % pixels > 240); white_end = first non-white frame after the run
    white_end first non-white frame (the held / smeared field picture follows)
    hold_end  start of the fade to black: first frame after the hold's luma peak with L < 0.9 * peak (first 5 game frames after
              the white are skipped; peak = max L between there and `dark`)
    dark      darkest frame between the white and the fade cut
    battle_in first frame after dark with L > dark + max(1.5, 25 %)  (battle fades in from black)
    fade_cut  first frame after the white (+5 gf) with L >= 40 and L >= 1.5 x the previous frame (battle picture appears)
    clear     first frame at / after the fade cut (fade_cut) whose g >= 0.85 * median g of the next 30 game frames
Ranges to capture: from 4 game frames before `contact` (route: start autoshot at the request, contact is within ~3 ticks)
to `clear` + 10 game frames.
"""
import site, sys, os, re, json, glob
sys.path.append(site.getusersitepackages())   # python -I drops the user site where numpy usually lives
import numpy as np
from PIL import Image
from multiprocessing.pool import ThreadPool

GF = 1000.0 / 60.0
W, H = 160, 112
BLUE_MIN = 0.01


def crop_for(im):
    w, h = im.size
    if h == 1024 and w == 1280:
        return im.crop((0, 32, 1280, 992))
    if h == 960 and w == 1280:
        return im.crop((0, 8, 1280, 952))
    return im


def load_rgb(path, size):
    return np.asarray(crop_for(Image.open(path).convert('RGB')).resize(size, Image.BOX), dtype=np.float32)


def blue_mask(a):
    return (a[..., 2] - np.maximum(a[..., 0], a[..., 1])) > 40


def frame_stats(path):
    a = load_rgb(path, (W, H))
    L = a @ np.array([.299, .587, .114], dtype=np.float32)
    return dict(L=float(L.mean()), b=float(blue_mask(a).mean()), g=float(np.abs(np.diff(L, axis=1)).mean()),
                w=float((L > 240).mean()))


def kind_of(names):
    rde = [n for n in names if re.match(r'f_\d+_', n)]
    ccx = [n for n in names if re.match(r'frame_\d+\.png$', n)]
    ours = [n for n in names if re.match(r'[A-Za-z0-9]+_\d+\.png$', n) and n not in rde and n not in ccx]
    best = max((rde, 'rde'), (ccx, 'ccx'), (ours, 'ours'), key=lambda x: len(x[0]))
    return best[1], best[0]


def time_of(kind, name):
    if kind == 'rde':
        return float(name.split('_')[1])
    return int(re.search(r'_(\d+)\.png$', name).group(1)) * GF


def collect(d):
    """Return dict of arrays (sorted by time): name, t(ms), L, b, g, w, plus kind.  DIR#ek_ restricts to a file prefix."""
    pre = ''
    if '#' in d:
        d, pre = d.split('#', 1)
    names = [n for n in os.listdir(d) if n.lower().endswith('.png') and n.startswith(pre)]
    kind, names = kind_of(names)
    if kind == 'ours' and not pre:   # a shots dir holds many prefixes: take the biggest group
        groups = {}
        for n in names:
            groups.setdefault(n.rsplit('_', 1)[0], []).append(n)
        pre = max(groups, key=lambda k: len(groups[k]))
        names = groups[pre]
    if not names:
        raise SystemExit(f'{d}: no recognised frames')
    names.sort(key=lambda n: time_of(kind, n))
    cp = os.path.join(d, 'trans_stats%s.json' % (('_' + pre.strip('_')) if pre else ''))
    cache = {}
    if os.path.exists(cp):
        try:
            cache = json.load(open(cp))
        except Exception:
            cache = {}
    todo = [n for n in names if n not in cache or cache[n].get('sz') != os.path.getsize(os.path.join(d, n))]
    if todo:
        def work(n):
            p = os.path.join(d, n)
            s = frame_stats(p)
            s['sz'] = os.path.getsize(p)
            return n, s
        with ThreadPool(12) as pool:
            for n, s in pool.imap_unordered(work, todo, chunksize=4):
                cache[n] = s
        json.dump(cache, open(cp, 'w'))
    t = np.array([time_of(kind, n) for n in names])
    out = dict(kind=kind, dir=d, name=names, t=t, pre=pre)
    for k in 'Lbgw':
        out[k] = np.array([cache[n][k] for n in names])
    return out


def at(S, key, t):
    return float(np.interp(t, S['t'], S[key]))


def segment_data(S):
    t, L, b, g, w = S['t'], S['L'], S['b'], S['g'], S['w']
    n = len(t)
    P = {}
    white = np.where(w > 0.9)[0]
    if len(white) == 0:
        return P
    i0 = white[0]
    j = i0
    while j + 1 < n and w[j + 1] > 0.9:
        j += 1
    P['white'] = i0
    P['white_end'] = min(j + 1, n - 1)
    # ring cluster before the white
    cand = [i for i in range(i0) if b[i] > BLUE_MIN and t[i] >= t[i0] - 8000]
    if cand:
        clusters, cur = [], [cand[0]]
        for i in cand[1:]:
            if t[i] - t[cur[-1]] <= 12 * GF:
                cur.append(i)
            else:
                clusters.append(cur); cur = [i]
        clusters.append(cur)
        cl = max(clusters, key=lambda c: b[c].sum())
        P['contact'] = cl[0]
        P['ring_peak'] = cl[int(np.argmax(b[cl]))]
        P['swirl'] = min(cl[-1] + 1, i0)
    te = t[P['white_end']]
    after = [i for i in range(P['white_end'], n) if t[i] >= te + 5 * GF]
    if not after:
        return P
    # fade cut first (the brightness jump into the battle), then the darkest frame / hold plateau before it
    cut = next((i for i in after if L[i] >= 40 and L[i] >= 1.5 * L[i - 1]), None)
    stop = cut if cut is not None else n
    seg = [i for i in after if i < stop]
    if not seg:
        return P
    mi = min(seg, key=lambda i: L[i])
    P['dark'] = mi
    pk = max((i for i in seg if i <= mi), key=lambda i: L[i])
    he = next((i for i in seg if i > pk and L[i] < 0.9 * L[pk]), None)
    if he is not None:
        P['hold_end'] = he
    thr = L[mi] + max(1.5, 0.25 * L[mi])
    bi = next((i for i in range(mi, n) if L[i] > thr), None)
    if bi is not None:
        P['battle_in'] = bi
    if cut is None:
        return P
    P['fade_cut'] = cut
    gm = np.median([g[i] for i in range(cut, n) if t[i] <= t[cut] + 30 * GF])
    P['clear'] = next((i for i in range(cut, n) if g[i] >= 0.85 * gm), cut)
    return P


ORDER = ['contact', 'ring_peak', 'swirl', 'white', 'white_end', 'hold_end', 'dark', 'battle_in', 'fade_cut', 'clear']
INTERVALS = [('ring', 'contact', 'swirl'), ('smear', 'swirl', 'white'), ('white', 'white', 'white_end'),
             ('hold', 'white_end', 'hold_end'), ('fade_out', 'hold_end', 'dark'), ('dark', 'dark', 'battle_in'),
             ('fade_in', 'battle_in', 'fade_cut')]


def cmd_segment(d):
    S = collect(d)
    P = segment_data(S)
    if 'white' not in P:
        print(f'{d}: no white clear frame found ({len(S["t"])} frames, kind {S["kind"]})')
        return
    tw = S['t'][P['white']]
    print(f'{d}  [{S["kind"]}, {len(S["t"])} frames, {(S["t"][-1]-S["t"][0])/1000:.1f} s]')
    print(f'{"phase":10s} {"frame":26s} {"t ms":>8s} {"gf vs white":>11s} {"L":>6s} {"g":>6s} {"b":>6s}')
    for k in ORDER:
        if k in P:
            i = P[k]
            print(f'{k:10s} {S["name"][i]:26s} {S["t"][i]:8.0f} {(S["t"][i]-tw)/GF:11.1f} {S["L"][i]:6.1f} {S["g"][i]:6.2f} {S["b"][i]:6.3f}')
        else:
            print(f'{k:10s} -')
    lo = P.get('contact', P['white'])
    hi = max(P.values())
    nb, na = lo, len(S['t']) - 1 - hi
    print(f'window: {S["name"][lo]} .. {S["name"][hi]}  = {(S["t"][hi]-S["t"][lo])/GF:.0f} game frames; '
          f'ignored {nb} frames before, {na} after' + ('' if 'clear' in P else '   (INCOMPLETE: no fade cut / clear frame in this capture)'))


# ---------------------------------------------------------------- rings
def ring_metrics(path, centre=None):
    a = load_rgb(path, (320, 240))
    m = blue_mask(a)
    ys, xs = np.nonzero(m)
    res = {'mask_px': int(m.sum())}
    if len(xs) < 20:
        res['note'] = 'no blue mask'
        return res
    out = {}
    cents = {'centroid': (xs.mean(), ys.mean()), 'screen': (160.0, 120.0)}
    if centre:
        cents = {'given': (centre[0] * 320, centre[1] * 240)}
    for nm, (cx, cy) in cents.items():
        r = np.hypot(xs - cx, ys - cy)
        rr = np.hypot(*np.meshgrid(np.arange(320) - cx, np.arange(240) - cy))
        nb = int(rr.max() / 2) + 1
        cnt = np.bincount((r / 2).astype(int), minlength=nb).astype(float)
        area = np.bincount((rr / 2).astype(int).ravel(), minlength=nb).astype(float)
        prof = cnt / np.maximum(area, 1)
        ps = np.convolve(prof, [1, 2, 3, 2, 1], 'same') / 9
        mx = ps.max()
        pk = [i for i in range(1, nb - 1) if ps[i] >= ps[i - 1] and ps[i] > ps[i + 1] and ps[i] > 0.4 * mx]
        # merge peaks separated by shallow valleys
        merged = []
        for i in pk:
            if merged and ps[merged[-1]:i + 1].min() > 0.75 * min(ps[i], ps[merged[-1]]):
                if ps[i] > ps[merged[-1]]:
                    merged[-1] = i
            else:
                merged.append(i)
        ip = int(np.argmax(ps))
        rad = 2 * ip
        sel = np.abs(r - rad) <= 8
        ang = np.arctan2(ys[sel] - cy, xs[sel] - cx)
        k = 24
        hist = np.histogram(ang, bins=k, range=(-np.pi, np.pi))[0].astype(float)
        # coverage relative to the sector's annulus area
        yy, xx = np.mgrid[0:240, 0:320]
        ra = np.hypot(xx - cx, yy - cy)
        ann = np.abs(ra - rad) <= 8
        aa = np.histogram(np.arctan2(yy[ann] - cy, xx[ann] - cx), bins=k, range=(-np.pi, np.pi))[0].astype(float)
        ok = aa > 0.5 * np.median(aa)
        cov = hist[ok] / aa[ok]
        cv = float(cov.std() / max(cov.mean(), 1e-6))
        out[nm] = dict(centre=(round(cx / 320, 3), round(cy / 240, 3)), radius=round(rad / 240, 3), peaks=len(merged),
                       width=round(float(np.sqrt((prof[: nb] * (np.arange(nb) * 2 - rad) ** 2).sum() / max(prof.sum(), 1e-9))) / 240, 3),
                       ang_cv=round(cv, 3), cover=round(float(cov.mean()), 3))
    res.update(out)
    return res


def cmd_rings(path, centre=None):
    r = ring_metrics(path, centre)
    print(f'{os.path.basename(path)}: blue mask {r["mask_px"]} px of 76800')
    for k, v in r.items():
        if isinstance(v, dict):
            print(f'  about {k:8s} centre ({v["centre"][0]:.3f},{v["centre"][1]:.3f})  radius {v["radius"]:.3f} H  width {v["width"]:.3f} H  '
                  f'radial peaks {v["peaks"]}  angular CV {v["ang_cv"]:.3f}  ring coverage {v["cover"]:.2f}')
    if 'note' in r:
        print('  ' + r['note'])


# ---------------------------------------------------------------- table
def slope(S, key, i0, i1):
    sel = [i for i in range(i0, i1 + 1)]   # inclusive
    if len(sel) < 3:
        return float('nan')
    x = (S['t'][sel] - S['t'][sel[0]]) / GF
    return float(np.polyfit(x, S[key][sel], 1)[0])


def cmd_table(dirs, step):
    srcs = []
    for lab, d in zip(('RDE', 'ccx', 'ours'), dirs):
        if d == '-':
            continue
        S = collect(d)
        P = segment_data(S)
        if 'white' not in P:
            print(f'{lab}: no white frame in {d}; skipped')
            continue
        S['P'], S['lab'] = P, lab
        S['tw'] = S['t'][P['white']]
        srcs.append(S)
    print('alignment: game frame 0 = first flat white frame of each source; durations in game frames (1/60 s)')
    print('\nPer-phase (L = mean luma 0..255 at phase start -> end, g = sharpness start -> end, dg/gf = linear slope of g):')
    print(f'{"phase":9s}' + ''.join(f'| {s["lab"]:^46s}' for s in srcs))
    print(f'{"":9s}' + ''.join(f'| {"dur   L0 -> L1    g0 -> g1  dg/gf dL/gf":46s}' for s in srcs))
    for nm, a, b in INTERVALS:
        row = f'{nm:9s}'
        for S in srcs:
            P = S['P']
            if a in P and b in P:
                ta, tb = S['t'][P[a]], S['t'][P[b]]
                ib = P[b] - 1 if nm in ('smear', 'hold') else P[b]   # last frame of the phase (the next one is white / the cut)
                tl = S['t'][max(ib, P[a])]
                sl = (slope(S, 'g', P[a], ib), slope(S, 'L', P[a], ib)) if nm != 'white' else (float('nan'),) * 2
                row += f'| {(tb-ta)/GF:5.1f} {at(S,"L",ta):6.1f} -> {at(S,"L",tl):6.1f} {at(S,"g",ta):5.2f} -> {at(S,"g",tl):5.2f} {sl[0]:6.3f} {sl[1]:6.2f}'
            else:
                row += f'| {"-":46s}'
        print(row)
    print('\nEvent times relative to the white start (game frames):')
    evs = ['contact', 'swirl', 'white', 'white_end', 'hold_end', 'dark', 'battle_in', 'fade_cut', 'clear']
    print(f'{"event":10s}' + ''.join(f'{s["lab"]:>9s}' for s in srcs))
    for e in evs:
        print(f'{e:10s}' + ''.join((f'{(s["t"][s["P"][e]]-s["tw"])/GF:9.1f}' if e in s['P'] else f'{"-":>9s}') for s in srcs))
    print('\nBlue ring at the peak-b frame (contact effect): radial peaks / angular CV about the mask centroid, radius (H), b')
    for S in srcs:
        P = S['P']
        if 'ring_peak' in P:
            r = ring_metrics(os.path.join(S['dir'], S['name'][P['ring_peak']]))
            c = r.get('centroid')
            if c:
                print(f'  {S["lab"]:5s} {S["name"][P["ring_peak"]]:24s} b {S["b"][P["ring_peak"]]:.3f}  peaks {c["peaks"]}  angular CV {c["ang_cv"]:.3f}  '
                      f'radius {c["radius"]:.3f}  width {c["width"]:.3f}  centre ({c["centre"][0]:.2f},{c["centre"][1]:.2f})')
            else:
                print(f'  {S["lab"]:5s} no blue mask')
    # luma table
    lo = min((S['t'][S['P']['contact']] - S['tw']) / GF if 'contact' in S['P'] else -30 for S in srcs)
    hi = max((S['t'][S['P'].get('clear', S['P']['white_end'])] - S['tw']) / GF for S in srcs)
    k0 = int(np.floor(lo / step)) * step
    labs = [s['lab'] for s in srcs]
    print(f'\nMean luma per {step} game frames (rel. white start; blank = outside that source\'s window):')
    hdr = f'{"gf":>5s}' + ''.join(f'{l:>8s}' for l in labs)
    pairs = [(i, j) for i in range(len(srcs)) for j in range(i + 1, len(srcs))]
    hdr += ''.join(f'{labs[j]+"-"+labs[i]:>11s}' for i, j in pairs)
    print(hdr)
    k = k0
    while k <= hi + step:
        vals = []
        for S in srcs:
            t = S['tw'] + k * GF
            P = S['P']
            a = S['t'][P.get('contact', P['white'])]
            b_ = S['t'][P.get('clear', P['white_end'])]
            vals.append(at(S, 'L', t) if a - 0.5 * GF <= t <= b_ + 0.5 * GF else None)
        row = f'{k:5d}' + ''.join(f'{v:8.1f}' if v is not None else f'{"":>8s}' for v in vals)
        for i, j in pairs:
            row += f'{vals[j]-vals[i]:+11.1f}' if vals[i] is not None and vals[j] is not None else f'{"":>11s}'
        print(row)
        k += step


def main(argv):
    if len(argv) < 3 or argv[1] not in ('segment', 'table', 'rings'):
        print(__doc__)
        return 1
    cmd = argv[1]
    if cmd == 'segment':
        cmd_segment(argv[2])
    elif cmd == 'rings':
        c = None
        if '--centre' in argv:
            i = argv.index('--centre')
            c = (float(argv[i + 1]), float(argv[i + 2]))
        cmd_rings(argv[2], c)
    else:
        step = 10
        a = list(argv[2:])
        if '--step' in a:
            i = a.index('--step')
            step = int(a[i + 1])
            del a[i:i + 2]
        if len(a) != 3:
            print('table needs RDE_DIR CCX_DIR OURS_DIR')
            return 1
        cmd_table(a, step)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
