"""Per-emitter live object counts: ccx state vs our OC_FX_OBJECTS log.   cmp_objs.py REFDIR OURS.log OFFSET FIRST LAST STEP
(ref frame F <-> our fx dump 'fx objects tick F+OFFSET'; OURS.log made with OC_FX_OBJECTS=<step>)."""
import sys, re, subprocess, collections, os
ref, log, off, f0, f1, st = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
ours = {}
cur = None
for ln in open(log, errors='replace'):
    m = re.match(r'\[D\] fx objects tick (\d+)', ln)
    if m: cur = int(m.group(1)); ours[cur] = collections.Counter(); continue
    m = re.match(r'\[D\]\s+(em|ob)\s+(\S+)\s+spr\s+(\S+)', ln)
    if m and cur is not None: ours[cur][m.group(2)[:4] + '/' + m.group(3)[:4]] += 1
for f in range(f0, f1 + 1, st):
    p = os.path.join(ref, 'states', 'state_%06d.sav' % f)
    if not os.path.exists(p) or (f + off) not in ours: continue
    out = subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), 'fx_ref.py'), 'objects', p], capture_output=True, text=True).stdout
    rc = collections.Counter()
    for ln in out.splitlines():
        m = re.match(r'\s+0x\w+\s+(em|ob)\s+(\S+)\s+spr\s+(\S+)', ln)
        if m: rc[m.group(2)[:4].rstrip('.') + '/' + m.group(3)[:4].rstrip('.')] += 1
    oc = ours[f + off]
    keys = sorted(set(rc) | set(oc))
    diff = ['%s %d|%d' % (k, rc[k], oc[k]) for k in keys if rc[k] != oc[k]]
    print('F%d (tick %d): ref %d ours %d  diffs: %s' % (f, f + off, sum(rc.values()), sum(oc.values()), ', '.join(diff) or 'none'))
