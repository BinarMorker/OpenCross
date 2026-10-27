"""Side-by-side mean luma, retail (ps1 run frame f) vs ours (tick f + 9):  python tools/py/trans_cmp.py retail.txt ours.txt first last step"""
import sys
r = [l.split() for l in open(sys.argv[1])]; o = [l.split() for l in open(sys.argv[2])]
v = lambda row: row[2] if row[1] == 'L' else row[1][1:]
for f in range(int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])):
    if f + 9 < len(o): print(f, 'retail', v(r[f]), ' ours', v(o[f + 9]))
