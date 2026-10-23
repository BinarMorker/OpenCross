"""Frame-time statistics of an OpenCross log split by phase of the field -> battle transition: swirl (battle requested until the
'enct' effect ends), hold (until the battle takes over) and battle (the first frames of the battle). Prints n / mean / p99 / max per
phase and every frame slower than 17.5 ms with the last battle load step timing seen before it.

    python tools/py/v_ft.py LOGFILE
"""
import re,sys
# phases from the log order: swirl (request -> 'enct' ended), hold/smear (-> take-over 'battle: field encounter'), battle (rest, first 200 frames)
ph={'swirl':[], 'hold':[], 'battle':[]}; cur=None; slow=[]; lastload=''
for l in open(sys.argv[1],errors='ignore'):
    if 'field: battle requested' in l: cur='swirl'
    elif "'enct' ended" in l: cur='hold'
    elif 'battle: field encounter id' in l: cur='battle'
    m=re.search(r'timing: battle load step (\d+): ([\d.]+) ms',l)
    if m: lastload=f'step{m[1]}={m[2]}'
    m=re.search(r'frametime: frame (\d+) tick (\d+) ([\d.]+) ms',l)
    if m and cur:
        v=float(m[3]); ph[cur].append(v)
        if v>17.5: slow.append((cur,int(m[1]),int(m[2]),v,lastload)); lastload=''
for k,a in ph.items():
    if k=='battle': a=a[3:200]
    a=sorted(a)
    if a: print(f"{k:7} n={len(a)} mean={sum(a)/len(a):.1f} p99={a[min(len(a)-1,int(len(a)*.99))]:.1f} max={a[-1]:.1f}")
for s in slow: print(' >17.5:',s)
