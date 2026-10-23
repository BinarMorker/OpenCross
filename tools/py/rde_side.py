"""Side-by-side montage RDE (top row) vs ours (bottom row) at given ms after the swirl's white.
python tools/py/rde_side.py RDEDIR out.png ms [ms ...]   (ours = tests/routes/shots/ek_NNN.png, tick = white tick + ms * 0.06)"""
import sys, os, glob
import numpy as np, cv2
from PIL import Image
d, outp, ms = sys.argv[1], sys.argv[2], [int(x) for x in sys.argv[3:]]
fs = sorted(os.listdir(d))
def L(p, crop): return np.asarray(Image.open(p).crop(crop).convert('L').resize((160, 120), Image.BOX), dtype=np.float32).mean()
ws = [int(f.split('_')[1]) for f in fs if L(os.path.join(d, f), (0, 32, 1280, 992)) > 200]
tw = [w for w in ws if w >= ws[-1] - 400][0]
ofs = sorted(glob.glob('tests/routes/shots/ek_*.png'))
ow = next(int(os.path.basename(f)[3:-4]) for f in ofs if L(f, (0, 8, 1280, 952)) > 200)
top, bot = [], []
for m in ms:
    f = min(fs, key=lambda f: abs(int(f.split('_')[1]) - tw - m)); im = cv2.imread(os.path.join(d, f))[32:992]
    im = cv2.resize(im, (320, 240)); cv2.putText(im, f'RDE {m}', (3, 14), 0, .45, (0, 255, 255), 1); top.append(im)
    n = ow + int(round(m * 0.06)); p = f'tests/routes/shots/ek_{n:02d}.png'
    im = cv2.imread(p)[8:952]; im = cv2.resize(im, (320, 240)); cv2.putText(im, f'ours {m} (tick {n})', (3, 14), 0, .45, (0, 255, 255), 1); bot.append(im)
rows = []
for i in range(0, len(ms), 5):
    rows.append(np.hstack(top[i:i+5] + [np.zeros_like(top[0])] * (5 - len(top[i:i+5])))); rows.append(np.hstack(bot[i:i+5] + [np.zeros_like(top[0])] * (5 - len(bot[i:i+5]))))
cv2.imwrite(outp, np.vstack(rows))
