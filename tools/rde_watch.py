"""Watch the running RDE (Steam) game window and print its brightest-of-burst mean luma every 0.5 s.

usage: rde_watch.py seconds
"""
import sys,time; sys.path.insert(0,'tools')
from rde_win import *
import numpy as np
h=find(); r=client_rect(h); t0=time.time()
with mss.mss() as s:
    while time.time()-t0<float(sys.argv[1]):
        best=0
        for _ in range(12):
            a=np.frombuffer(s.grab(r).rgb,np.uint8)[::13]; best=max(best,a.mean()); time.sleep(.01)
        print(round(time.time()-t0,1),round(best,1),end=' | ')
