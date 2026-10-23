"""Tile n evenly spaced frames of a capture directory into one 4-column montage PNG.

usage: rde_montage.py dir out.png n [start_idx end_idx]
"""
import sys,os,cv2,numpy as np
d=sys.argv[1]; fs=sorted(os.listdir(d)); n=int(sys.argv[3])
a=int(sys.argv[4]) if len(sys.argv)>4 else 0; b=int(sys.argv[5]) if len(sys.argv)>5 else len(fs)
idx=np.linspace(a,b-1,n).astype(int); tiles=[]
for i in idx:
    im=cv2.imread(os.path.join(d,fs[i]))[32:992]; im=cv2.resize(im,(320,240)); cv2.putText(im,fs[i][2:9]+' '+str(i),(4,14),0,.45,(0,255,255),1); tiles.append(im)
while len(tiles)%4: tiles.append(np.zeros_like(tiles[0]))
rows=[np.hstack(tiles[i:i+4]) for i in range(0,len(tiles),4)]
cv2.imwrite(sys.argv[2],np.vstack(rows))
