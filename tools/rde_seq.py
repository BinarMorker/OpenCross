"""Press a key in the running RDE (Steam) game window and save a montage of thumbnails taken afterwards.

usage: rde_seq.py out.png key|- [n] [dt]   (montage of n thumbnails, dt s apart, after pressing key)
"""
import sys,time; sys.path.insert(0,'tools')
from rde_win import *
import pydirectinput
from PIL import Image
key=sys.argv[2]; n=int(sys.argv[3]) if len(sys.argv)>3 else 8; dt=float(sys.argv[4]) if len(sys.argv)>4 else .6
h=find(); front(h); r=client_rect(h); ims=[]
with mss.mss() as s:
    if key!='-':
        pydirectinput.keyDown(key); time.sleep(.1); pydirectinput.keyUp(key)
    for i in range(n):
        time.sleep(dt)
        for _ in range(30):
            im=s.grab(r)
            if max(im.rgb[::997])>0: break
            time.sleep(.02)
        ims.append(Image.frombytes('RGB',im.size,im.rgb).resize((320,256)))
W=Image.new('RGB',(1280,256*((n+3)//4)))
for i,m in enumerate(ims): W.paste(m,((i%4)*320,(i//4)*256))
W.save(sys.argv[1])
