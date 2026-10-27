"""RDE capture analysis:  python -I tools/py/rde_trans_analyze.py DIR [DIR ...]
Per real frame (frames named f_<ms>_<mean*10>.png from tools/rde_capture.py; picture = rows 32..992 of the 1280x1024
client): luma L (PIL 'L', same as ps1 trans_measure), mean abs gradient x/y at 320x240 (blur metric), white%/dark%.
Time is relative to the first white frame (L > 200) found after a field picture (t = 0), in ms.  Prints one table per DIR
(t, L, grad, white%, dark%) and a merged curve over all DIRs (50 ms bins, mean L)."""
import sys, os
import numpy as np
from PIL import Image

def frames(d):
    out = []
    for f in sorted(os.listdir(d)):
        t = int(f.split('_')[1])
        im = Image.open(os.path.join(d, f)).convert('L').crop((0, 32, 1280, 992))
        g = np.asarray(im.resize((320, 240), Image.BOX), dtype=np.float32)
        out.append((t, float(g.mean()), float(np.abs(np.diff(g, axis=1)).mean()), float(np.abs(np.diff(g, axis=0)).mean()),
                    100 * float((g > 240).mean()), 100 * float((g < 16).mean()), f))
    return out

def t_white(fr):
    # the swirl's white: first frame with L > 200 that follows a frame with L < 150 (skips the results exit white fade)
    cand = [i for i, r in enumerate(fr) if r[1] > 200]
    return [fr[i][0] for i in cand]

if __name__ == '__main__':
    bins = {}
    for d in sys.argv[1:]:
        fr = frames(d)
        ws = t_white(fr)
        print(f'## {d}: {len(fr)} frames, white frames at ms: {ws}')
        tw = ws[-1] if False else None
        # the encounter's white = the LAST white run (the results exit fade is the earlier one)
        runs = []
        for w in ws:
            if runs and w - runs[-1][-1] < 400: runs[-1].append(w)
            else: runs.append([w])
        print('   white runs:', runs)
        if not runs: continue
        t0 = runs[-1][0]
        for r in fr:
            if r[0] < t0 - 1500 or r[0] > t0 + 5200: continue
            print(f'{r[0]-t0:6d} L{r[1]:6.1f} gx {r[2]:5.2f} gy {r[3]:5.2f} white {r[4]:5.1f}% dark {r[5]:5.1f}%  {r[6]}')
            bins.setdefault(int(round((r[0] - t0) / 50.0)), []).append(r[1])
    print('## merged 50 ms bins (t, n, mean L)')
    for k in sorted(bins): print(f'{k*50:6d} {len(bins[k])} {np.mean(bins[k]):6.1f}')
