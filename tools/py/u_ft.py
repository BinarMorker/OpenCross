"""Frame-time statistics from an OpenCross log: reads `frametime: frame N tick T X ms` lines and prints n / mean / p99 / max of the
frame time overall and for each window of 40 logged frames.

    python tools/py/u_ft.py LOGFILE
"""
import re,sys,statistics as st
rows=[]
for l in open(sys.argv[1],errors='ignore'):
    m=re.search(r'frametime: frame (\d+) tick (\d+) ([\d.]+) ms',l)
    if m: rows.append((int(m[2]),float(m[3])))
t0=rows[0][0]
# phases by tick offset from first logged tick: swirl <?, white,..; print per-window stats
def stats(a):
    a=sorted(a); 
    return f"n={len(a)} mean={sum(a)/len(a):.1f} p99={a[min(len(a)-1,int(len(a)*.99))]:.1f} max={a[-1]:.1f}"
print("all",stats([r[1] for r in rows[1:]]))
for lo in range(0,len(rows),40):
    seg=rows[lo:lo+40]
    print(seg[0][0],seg[-1][0],stats([r[1] for r in seg]))
