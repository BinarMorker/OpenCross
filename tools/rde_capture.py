"""Capture the real RDE (Steam) field->battle transition frame by frame.

Prereqs: CHRONOCROSS.exe running windowed (config ScreenMode=2), a save loaded and the party standing where
holding the chosen arrow key walks into an encounter (save 15 of the owner: load it, walk down out of the room into the
hall, then right along the carpet).  Run (python, pywin32 + pydirectinput + windows-capture + opencv):
    python tools/rde_capture.py [outdir] [key=right | comma list e.g. down,down,w5,hold:right:3] [max_s=40] [after_white_s=14] [pre_s=5]
The window is captured with Windows.Graphics.Capture (mss/BitBlt returns exactly-black frames about a third of the
time for this window; WGC gives every presented frame, ~55 fps).  Frames are written as <outdir>/f_<ms>_<mean>.png
(client area only, 1280x1024 incl. the letterbox bars), time = ms since capture start.  The key is held until the first
white frames (mean > 240) are seen; then `after_white_s` more seconds are recorded and everything older than
`pre_s` seconds before the first trigger (white or a luma jump > 30) is deleted.  Black captures (mean < 0.3, window artefact) are not stored.
"""
import sys,os,time,threading,queue,ctypes
ctypes.windll.user32.SetProcessDPIAware()
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import numpy as np,cv2,pydirectinput
import rde_win
from windows_capture import WindowsCapture,Frame,InternalCaptureControl
out=sys.argv[1] if len(sys.argv)>1 else 'tmp/rde_trans/frames'
key=sys.argv[2] if len(sys.argv)>2 else 'right'
max_s=float(sys.argv[3]) if len(sys.argv)>3 else 40
after=float(sys.argv[4]) if len(sys.argv)>4 else 14
pre=float(sys.argv[5]) if len(sys.argv)>5 else 6
os.makedirs(out,exist_ok=True)
for f in os.listdir(out): os.remove(os.path.join(out,f))
h=rde_win.find(); rde_win.front(h); time.sleep(.4)
q=queue.Queue(); t0=time.perf_counter(); st={'white':None,'n':0,'black':0,'last':None}
log=[]
def writer():
    while True:
        it=q.get()
        if it is None: break
        name,img=it; cv2.imwrite(os.path.join(out,name),img,[cv2.IMWRITE_PNG_COMPRESSION,1])
ws=[threading.Thread(target=writer) for _ in range(4)]
[w.start() for w in ws]
cap=WindowsCapture(cursor_capture=False,draw_border=False,window_name='CHRONO CROSS')
@cap.event
def on_frame_arrived(frame:Frame,ctl:InternalCaptureControl):
    a=frame.frame_buffer
    H,W=a.shape[:2]; y0=H-1024-1; x0=(W-1280)//2
    c=np.ascontiguousarray(a[y0:y0+1024,x0:x0+1280,:3])   # BGR
    t=(time.perf_counter()-t0)*1000
    m=float(c.mean())
    if m<0.3: st['black']+=1; return      # the window alternates with black captures: dropped
    st['n']+=1; log.append((t,m))
    if st['white'] is None and (m>200 or (not os.environ.get('RDE_WHITE_ONLY') and st['last'] is not None and abs(m-st['last'][1])>30 and t-st['last'][0]<400)): st['white']=t   # RDE_WHITE_ONLY=1: room fades do not trigger
    st['last']=(t,m)
    q.put((f'f_{int(t):07d}_{int(m*10):04d}.png',c))
@cap.event
def on_closed(): pass
ctl=cap.start_free_threaded(); time.sleep(1.0)
def run_seq(seq):
    for tok in seq.split(','):
        if tok.startswith('w'): time.sleep(float(tok[1:]))
        elif tok.startswith('hold:'):    # hold:key:seconds
            _,k,d=tok.split(':'); ks=k.split('+')   # hold:right+down:3 = two keys together (diagonal)
            [pydirectinput.keyDown(x) for x in ks]; time.sleep(float(d)); [pydirectinput.keyUp(x) for x in ks]
        else: pydirectinput.keyDown(tok); time.sleep(.1); pydirectinput.keyUp(tok); time.sleep(.2)
if ',' in key or key.startswith('w') or key.startswith('hold:'): threading.Thread(target=run_seq,args=(key,),daemon=True).start()
else: pydirectinput.keyDown(key)
tk=time.perf_counter()
while time.perf_counter()-t0<max_s:
    time.sleep(.05)
    if st['white'] is not None and (time.perf_counter()-t0)*1000>st['white']+after*1000: break
    if False:   # rolling-window pruning disabled: every frame is kept until the end
        for f in os.listdir(out):
            if int(f.split('_')[1])<(time.perf_counter()-t0)*1000-pre*1000-25000:
                try: os.remove(os.path.join(out,f))
                except OSError: pass
if not (',' in key or key.startswith('w') or key.startswith('hold:')): pydirectinput.keyUp(key)
try: ctl.stop()
except Exception as e: print('stop',e)
[q.put(None) for _ in ws]; [w.join() for w in ws]
if st['white'] is not None and pre<900:
    for f in os.listdir(out):
        if int(f.split('_')[1])<st['white']-pre*1000: os.remove(os.path.join(out,f))
print('frames',st['n'],'exact-black',st['black'],'first white ms',st['white'],'files',len(os.listdir(out)))
