"""Mean absolute luma gradient (blur metric) + radial streak metric per frame:  python tools/py/trans_sharp.py DIR first last"""
import sys, os
import numpy as np
from PIL import Image
d, a, b = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
crop = tuple(int(v) for v in os.environ['CROP'].split(',')) if os.environ.get('CROP') else None
for n in range(a, b + 1):
    p = os.path.join(d, f'frame_{n:06d}.png')
    if not os.path.exists(p): continue
    im = Image.open(p).convert('L')
    if crop: im = im.crop((crop[0], crop[1], crop[0]+crop[2], crop[1]+crop[3]))
    g = np.asarray(im.resize((320, 224), Image.BOX), dtype=np.float32)
    gx = np.abs(np.diff(g, axis=1)).mean(); gy = np.abs(np.diff(g, axis=0)).mean()
    print(f'{n:4d} L{g.mean():6.1f} grad x {gx:5.2f} y {gy:5.2f}')
