"""Press keys in the running RDE (Steam) game window and save a screenshot.

usage: rde_key.py out.png [key | wSECONDS ...]   (a bare key is tapped; wN waits N seconds; the shot is 640x512)
"""
import sys,time,ctypes
sys.path.insert(0,'tools')
from rde_win import *
import pydirectinput
pydirectinput.PAUSE=0.02
h=find(); front(h); time.sleep(.3)
print('fg ok',win32gui.GetForegroundWindow()==h)
for a in sys.argv[2:]:
    if a.startswith('w'): time.sleep(float(a[1:]))
    else: pydirectinput.keyDown(a); time.sleep(.12); pydirectinput.keyUp(a); time.sleep(.3)
shot(sys.argv[1],(640,512))
