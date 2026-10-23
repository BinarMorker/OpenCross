"""Best-of-burst screenshot of the running RDE (Steam) game window (the window alternates with black captures).

usage: rde_snap.py out.png [width=640] [height=512]
"""
import sys,time; sys.path.insert(0,'tools')
from rde_win import *
import numpy as np
from PIL import Image
h=find(); r=client_rect(h)
with mss.mss() as s:
    best=None
    for _ in range(30):
        im=s.grab(r); m=np.frombuffer(im.rgb,np.uint8)[::13].mean()
        if best is None or m>best[0]: best=(m,im)
        time.sleep(.005)
Image.frombytes('RGB',best[1].size,best[1].rgb).resize((int(sys.argv[2]) if len(sys.argv)>2 else 640,)*1 + (int(sys.argv[3]) if len(sys.argv)>3 else 512,)).save(sys.argv[1])
