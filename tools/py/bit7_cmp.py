"""Compare the matrices of the flag bit 7 + 15 objects (attach joint rotation x object matrix) between a ccx capture and ours.

    python tools/py/bit7_cmp.py CCX_MAT_TXT OURS_LOG [--offset N | --scan] [--win W]

CCX_MAT_TXT = `fx_ref.py objects STATES --bit7 --mat` output (PowerShell UTF-16 is fine), OURS_LOG = ours.log of a run with OC_BIT7_TRACE=2
(`bit7mat: t<emitter tick> <name> pos x y z mat m0..m8`, one line per drawn object).
Without --offset an object is matched by emitter name + integer position (any tick) and the closest of ours' matrices with that key is compared
(max |delta| over the 9 entries) - a rule check that ignores timing.  With --offset N (fx_diff's `our tick = ccx frame + offset`) the
ccx state frame F is compared with ours' ticks F + N - W .. F + N + W (default W 2) of the same name (the closest again): a timing-aware check.
Prints per name: ccx samples, matched, median / worst delta, and the three worst rows.
"""
import re
import sys
from collections import defaultdict


def read(path):
    raw = open(path, "rb").read()
    for enc in ("utf-16", "utf-8-sig", "cp1252"):
        try:
            t = raw.decode(enc)
            if "mat" in t:
                return t.splitlines()
        except UnicodeDecodeError:
            pass
    return raw.decode("latin-1").splitlines()


def dist(a, b):
    return max(abs(x - y) for x, y in zip(a, b))


def main(argv):
    offset = None
    scan = "--scan" in argv
    argv = [a for a in argv if a != "--scan"]
    win = 2
    args = []
    i = 0
    while i < len(argv):
        if argv[i] == "--offset":
            offset = int(argv[i + 1]); i += 2
        elif argv[i] == "--win":
            win = int(argv[i + 1]); i += 2
        else:
            args.append(argv[i]); i += 1
    ccx, state, cur = [], None, None
    for ln in read(args[0]):
        m = re.match(r"== state_(\d+)", ln)
        if m:
            state = int(m.group(1))
            continue
        m = re.search(r"em (.{4}) spr (\S+) pos\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)", ln)
        if m:
            cur = (state, m.group(1).strip(". "), (int(m.group(3)), int(m.group(4)), int(m.group(5))))
            continue
        m = re.match(r"\s+mat (.*)", ln)
        if m and cur:
            ccx.append((cur, [float(x) for x in m.group(1).split()]))
            cur = None
    by_pos, by_tick = defaultdict(list), defaultdict(list)
    for ln in read(args[1]):
        m = re.match(r"bit7mat: (?:t(\d+) )?(.{1,4}?)\s+pos (-?\d+) (-?\d+) (-?\d+) mat (.*)", ln)
        if not m:
            continue
        nm = m.group(2).strip(". ")
        mat = [float(x) for x in m.group(6).split()]
        by_pos[(nm, (int(m.group(3)), int(m.group(4)), int(m.group(5))))].append(mat)
        if m.group(1):
            by_tick[(nm, int(m.group(1)))].append(mat)
    if scan:  # the best offsets: most matched samples, then the lowest mean delta
        res = []
        for off in range(-500, 801):
            ds = []
            for (st, nm, pos), mat in ccx:
                c = [o for t in range(st + off - win, st + off + win + 1) for o in by_tick.get((nm, t), [])]
                if c:
                    ds.append(min(dist(mat, o) for o in c))
            if ds:
                res.append((-len(ds), sum(ds) / len(ds), off))
        for n, mean, off in sorted(res)[:5]:
            print("offset %d: matched %d mean delta %.3f" % (off, -n, mean))
        return
    per, miss = defaultdict(list), defaultdict(int)
    for (st, nm, pos), mat in ccx:
        if offset is None:
            c = by_pos.get((nm, pos), [])
        else:
            c = [o for t in range(st + offset - win, st + offset + win + 1) for o in by_tick.get((nm, t), [])]
        if not c:
            miss[nm] += 1
            continue
        best = min(c, key=lambda o: dist(mat, o))
        per[nm].append((dist(mat, best), st, pos, mat, best))
    print("ccx samples %d, ours keys %d%s" % (len(ccx), len(by_pos), "" if offset is None else ", offset %d +-%d" % (offset, win)))
    for nm in sorted(set(per) | set(miss)):
        d = sorted(x[0] for x in per[nm])
        print("%-5s matched %3d unmatched %3d  median %.3f worst %.3f" % (nm, len(d), miss[nm], d[len(d) // 2] if d else -1, d[-1] if d else -1))
    for dlt, st, pos, mat, om in sorted((x for v in per.values() for x in v), key=lambda x: -x[0])[:3]:
        print("worst %.3f state %d pos %s\n  ccx  %s\n  ours %s" % (dlt, st, pos, " ".join("%.3f" % x for x in mat), " ".join("%.3f" % x for x in om)))


main(sys.argv[1:])
