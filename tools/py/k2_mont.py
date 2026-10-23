"""Montage of RDE capture frames nearest to the requested times (4 tiles per row, 320x240, labelled with ms and luma).

    python tools/py/k2_mont.py analysis.txt framesdir out.png ms1,ms2,...    (ms relative to the white frame)

analysis.txt is the per-frame table of rde_trans_analyze.py (time, luma, ..., frame file name in the last column).
"""
import sys,os,cv2,numpy as np
lines=[l.split() for l in open(sys.argv[1]).read().split('\n')[2:156] if l.strip() and l[0]!='#']
want=[int(x) for x in sys.argv[4].split(',')]
tiles=[]
for w in want:
    r=min(lines,key=lambda l:abs(int(l[0])-w)); im=cv2.imread(os.path.join(sys.argv[2],r[-1]))[32:992]; im=cv2.resize(im,(320,240)); cv2.putText(im,r[0]+' L'+r[2],(4,14),0,.45,(0,255,255),1); tiles.append(im)
while len(tiles)%4: tiles.append(np.zeros_like(tiles[0]))
cv2.imwrite(sys.argv[3],np.vstack([np.hstack(tiles[i:i+4]) for i in range(0,len(tiles),4)]))
