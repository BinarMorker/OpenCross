"""Reference-frame helpers for the ccx save states (stdlib only, plus ccx_state.py's zstandard):

    python tools/py/vram_frame.py png <state.sav> <out.png> [x y w h]        a VRAM rectangle (15-bit) as an RGB PNG; default 0 0 320 224
                                                                             (the battle / field display buffer: SetDefDispEnv(x, 0, 320, 224),
                                                                             double buffered at x 0 / 320 - use x 320 for the other buffer)
    python tools/py/vram_frame.py resample <in.png> <out.png> W H [x y w h]  box-average a source rectangle (default: the whole image) to W x H,
                                                                             fractional boxes allowed (our 1280 x 960 shot: the picture rows
                                                                             8..951 hold 224 PS1 rows -> `resample shot.png out.png 320 224 0 8 1280 944`)
    python tools/py/vram_frame.py rgbgrid <A.png> cols rows [x y w h]        mean R G B per cell of a grid over a rectangle
    python tools/py/vram_frame.py mask <A.png> <B.png> thr                   pixels where A and B differ by > thr in any channel: count + bbox + mean of A
                                                                             and B over those pixels (the footprint of one object = with / without it)
    python tools/py/vram_frame.py maskstats <mask-A.png> <mask-B.png> thr <X.png> [<Y.png> ...]
                                                                             mean R G B of X (and Y ...) over the pixels where A / B differ (> thr)

All images are reduced to RGB; sizes must match where two are combined (resample first).
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from png_stats import read_png  # noqa: E402


def write_png(path, w, h, rgb):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb[y * w * 3:(y + 1) * w * 3]
    def chunk(t, b):
        c = struct.pack('>I', len(b)) + t + b
        return c + struct.pack('>I', zlib.crc32(t + b) & 0xFFFFFFFF)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    data += chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b'')
    open(path, 'wb').write(data)


def vram_png(state_path, out, box):
    from ccx_state import State
    st = State(state_path)
    x, y, w, h = box
    rgb = bytearray(w * h * 3)
    for r in range(h):
        row = struct.unpack_from('<%dH' % w, st.vram, (y + r) * 2048 + x * 2)
        o = r * w * 3
        for i, p in enumerate(row):
            rgb[o + i * 3] = (p & 31) << 3 | (p & 31) >> 2
            rgb[o + i * 3 + 1] = ((p >> 5) & 31) << 3 | ((p >> 5) & 31) >> 2
            rgb[o + i * 3 + 2] = ((p >> 10) & 31) << 3 | ((p >> 10) & 31) >> 2
    write_png(out, w, h, rgb)


def resample(img, W, H, box):
    w, h, rgb = img
    x0, y0, bw, bh = box
    out = bytearray(W * H * 3)
    # integral image per channel for exact fractional boxes would be heavy in pure python; use pixel-centre binning
    acc = [[0, 0, 0, 0] for _ in range(W * H)]
    for sy in range(bh):
        ty = min(H - 1, int((sy + 0.5) * H / bh))
        base = ((y0 + sy) * w + x0) * 3
        trow = ty * W
        for sx in range(bw):
            tx = min(W - 1, int((sx + 0.5) * W / bw))
            a = acc[trow + tx]
            s = base + sx * 3
            a[0] += rgb[s]
            a[1] += rgb[s + 1]
            a[2] += rgb[s + 2]
            a[3] += 1
    for i, a in enumerate(acc):
        n = max(1, a[3])
        out[i * 3], out[i * 3 + 1], out[i * 3 + 2] = a[0] // n, a[1] // n, a[2] // n
    return W, H, out


def mean_rgb(rgb, idx):
    n = len(idx)
    if not n:
        return (0.0, 0.0, 0.0)
    return (sum(rgb[i * 3] for i in idx) / n, sum(rgb[i * 3 + 1] for i in idx) / n, sum(rgb[i * 3 + 2] for i in idx) / n)


def mask_indices(a, b, thr):
    if a[0] != b[0] or a[1] != b[1]:
        raise SystemExit('mask: sizes differ')
    ra, rb = a[2], b[2]
    return [i for i in range(a[0] * a[1]) if abs(ra[i * 3] - rb[i * 3]) > thr or abs(ra[i * 3 + 1] - rb[i * 3 + 1]) > thr or abs(ra[i * 3 + 2] - rb[i * 3 + 2]) > thr]


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    cmd = argv[0]
    if cmd == 'png':
        box = tuple(int(v) for v in argv[3:7]) if len(argv) >= 7 else (0, 0, 320, 224)
        vram_png(argv[1], argv[2], box)
        print('wrote', argv[2], box)
    elif cmd == 'resample':
        img = read_png(argv[1])
        W, H = int(argv[3]), int(argv[4])
        box = tuple(int(v) for v in argv[5:9]) if len(argv) >= 9 else (0, 0, img[0], img[1])
        w, h, rgb = resample(img, W, H, box)
        write_png(argv[2], w, h, rgb)
        print('wrote', argv[2], W, H, 'from', box)
    elif cmd == 'rgbgrid':
        img = read_png(argv[1])
        cols, rows = int(argv[2]), int(argv[3])
        box = tuple(int(v) for v in argv[4:8]) if len(argv) >= 8 else (0, 0, img[0], img[1])
        w, h, rgb = img
        x0, y0, bw, bh = box
        for r in range(rows):
            cells = []
            for c in range(cols):
                idx = []
                for yy in range(y0 + r * bh // rows, y0 + (r + 1) * bh // rows):
                    idx.extend(range(yy * w + x0 + c * bw // cols, yy * w + x0 + (c + 1) * bw // cols))
                m = mean_rgb(rgb, idx)
                cells.append('%3.0f,%3.0f,%3.0f' % m)
            print(' | '.join(cells))
    elif cmd == 'mask':
        a, b = read_png(argv[1]), read_png(argv[2])
        idx = mask_indices(a, b, int(argv[3]))
        if not idx:
            print('no differing pixels')
            return 0
        xs = [i % a[0] for i in idx]
        ys = [i // a[0] for i in idx]
        print('%d pixels, bbox x %d..%d y %d..%d' % (len(idx), min(xs), max(xs), min(ys), max(ys)))
        print('mean A %.1f %.1f %.1f' % mean_rgb(a[2], idx))
        print('mean B %.1f %.1f %.1f' % mean_rgb(b[2], idx))
    elif cmd == 'maskstats':
        a, b = read_png(argv[1]), read_png(argv[2])
        idx = mask_indices(a, b, int(argv[3]))
        print('%d mask pixels' % len(idx))
        for p in argv[4:]:
            img = read_png(p)
            if img[0] != a[0] or img[1] != a[1]:
                raise SystemExit('maskstats: %s size differs' % p)
            print('%s: mean %.1f %.1f %.1f' % ((p,) + mean_rgb(img[2], idx)))
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
