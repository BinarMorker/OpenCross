"""ref_info.py REFDIR : per state of a ps1 capture: frame, live object count, camera eye/target/H (RAM 0x80071F68/70/84), fog near/far/colour."""
import sys, os, glob, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ps1_state as c, fx_ref
d = sys.argv[1]; step = int(sys.argv[2]) if len(sys.argv) > 2 else 10
for p in sorted(glob.glob(os.path.join(d, 'states', 'state_*.sav'))):
    f = int(os.path.basename(p)[6:12])
    if f % step: continue
    s = c.State(p)
    out = subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fx_ref.py'), 'objects', p], capture_output=True, text=True).stdout
    n = [l for l in out.splitlines() if ' em ' in l or ' ob ' in l]
    print('%4d objs %3d eye %6d %6d %6d tgt %6d %6d %6d H %d fog near %d far %d rgb %d %d %d' % (f, len(n), *[s.s16(0x80071F68 + 2 * i) for i in range(3)], *[s.s16(0x80071F70 + 2 * i) for i in range(3)], s.s16(0x80071F84), s.u16(0x8008dbb8), s.u16(0x8008dbb2), s.u16(0x8008dbb4), s.u16(0x8008dbb6) if False else 0, 0))
