"""Helpers for the rde_*.py scripts: find the RDE (Steam) game window, bring it to the front, grab its client area."""
import ctypes
ctypes.windll.user32.SetProcessDPIAware()
import win32gui,win32con,mss,time
from PIL import Image
def find(title='CHRONO CROSS'):
    r=[]
    def cb(h,_):
        if win32gui.IsWindowVisible(h) and win32gui.GetWindowText(h)==title: r.append(h)
    win32gui.EnumWindows(cb,None)
    return r[0] if r else None
def client_rect(h):
    l,t,rr,b=win32gui.GetClientRect(h)
    x,y=win32gui.ClientToScreen(h,(0,0))
    return {'left':x,'top':y,'width':rr,'height':b}
def front(h,x=0,y=0):
    import win32api
    win32gui.ShowWindow(h,win32con.SW_RESTORE)
    win32gui.SetWindowPos(h,win32con.HWND_TOPMOST,x,y,0,0,win32con.SWP_NOSIZE)
    win32api.keybd_event(0x12,0,0,0)
    try: win32gui.SetForegroundWindow(h)
    except Exception as e: print('fg',e)
    win32api.keybd_event(0x12,0,2,0)
    if win32gui.GetForegroundWindow()!=h:   # a higher-integrity dialog may hold the foreground: click the title bar
        l,t,r,b=win32gui.GetWindowRect(h)
        win32api.SetCursorPos(((l+r)//2,t+12)); time.sleep(.2)
        win32api.mouse_event(2,0,0);time.sleep(.05);win32api.mouse_event(4,0,0);time.sleep(.3)
def shot(path,scale=None):
    h=find(); r=client_rect(h)
    with mss.mss() as s:
        for _ in range(30):   # the window alternates with exactly-black captures: retry
            im=s.grab(r)
            if max(im.rgb[::997])>0: break
            time.sleep(.02)
        I=Image.frombytes('RGB',im.size,im.rgb)
    if scale: I=I.resize(scale)
    I.save(path); return r
if __name__=='__main__':
    import sys
    h=find(); front(h); time.sleep(.5); print(shot(sys.argv[1] if len(sys.argv)>1 else 'tmp/rde_trans/s.png'))
