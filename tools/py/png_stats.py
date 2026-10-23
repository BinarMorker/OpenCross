"""Pixel statistics for reference comparisons without any image library (stdlib zlib only; run with python -I).

    python -I tools/py/png_stats.py stats  A.png [x y w h]            mean R G B luma, size (box in source pixels)
    python -I tools/py/png_stats.py diff   A.png B.png [x y w h]      per-channel mean |diff|, % of pixels differing > 16 in luma
    python -I tools/py/png_stats.py grid   A.png cols rows            mean luma per cell of a cols x rows grid (one line per row)
    python -I tools/py/png_stats.py hist   A.png [x y w h]            16-bin luma histogram

diff / grid work on frames of different sizes: both are box-downsampled to the smaller size (integer ratios only), which is what
a ccx 640x448 frame vs our 1280x960 letterboxed shot needs once the shot is cropped to the picture (the 8/960 crop bands).
Supported PNGs: 8-bit grey / grey+alpha / RGB / RGBA, non-interlaced (what ccx and OpenCross write). Alpha is ignored.
"""
import struct
import sys
import zlib


def read_png(path):
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise SystemExit(f'{path}: not a PNG')
    pos, idat, w = 8, [], None
    while pos < len(data):
        ln, typ = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if typ == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
            if depth != 8 or interlace or ctype not in (0, 2, 4, 6):
                raise SystemExit(f'{path}: unsupported PNG (depth {depth} type {ctype} interlace {interlace})')
            ch = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
        elif typ == b'IDAT':
            idat.append(body)
        elif typ == b'IEND':
            break
    raw = zlib.decompress(b''.join(idat))
    stride = w * ch
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        if f == 1:
            for i in range(ch, stride):
                line[i] = (line[i] + line[i - ch]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                b = prev[i]
                c = prev[i - ch] if i >= ch else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    # to RGB triples (grey expanded, alpha dropped)
    if ch == 3:
        rgb = out
    else:
        rgb = bytearray(w * h * 3)
        if ch == 4:
            rgb[0::3], rgb[1::3], rgb[2::3] = out[0::4], out[1::4], out[2::4]
        elif ch == 1:
            rgb[0::3] = rgb[1::3] = rgb[2::3] = out
        else:
            rgb[0::3] = rgb[1::3] = rgb[2::3] = out[0::2]
    return w, h, rgb


def crop(img, box):
    w, h, rgb = img
    x, y, cw, chh = box
    out = bytearray(cw * chh * 3)
    for r in range(chh):
        s = ((y + r) * w + x) * 3
        out[r * cw * 3:(r + 1) * cw * 3] = rgb[s:s + cw * 3]
    return cw, chh, out


def downsample(img, fx, fy):
    w, h, rgb = img
    nw, nh = w // fx, h // fy
    out = bytearray(nw * nh * 3)
    n = fx * fy
    for yy in range(nh):
        for xx in range(nw):
            acc = [0, 0, 0]
            for dy in range(fy):
                base = ((yy * fy + dy) * w + xx * fx) * 3
                for dx in range(fx):
                    acc[0] += rgb[base + dx * 3]
                    acc[1] += rgb[base + dx * 3 + 1]
                    acc[2] += rgb[base + dx * 3 + 2]
            o = (yy * nw + xx) * 3
            out[o], out[o + 1], out[o + 2] = acc[0] // n, acc[1] // n, acc[2] // n
    return nw, nh, out


def match(a, b):
    """Downsample the larger of a / b to the smaller size (integer ratio required)."""
    if a[0] == b[0] and a[1] == b[1]:
        return a, b
    big, small, swap = (a, b, False) if a[0] > b[0] else (b, a, True)
    if big[0] % small[0] or big[1] % small[1]:
        raise SystemExit(f'sizes {a[0]}x{a[1]} vs {b[0]}x{b[1]} are not integer multiples: crop first')
    big = downsample(big, big[0] // small[0], big[1] // small[1])
    return (small, big) if swap else (big, small)


def luma(r, g, b):
    return (r * 299 + g * 587 + b * 114) // 1000


def stats(img):
    w, h, rgb = img
    n = w * h
    sr, sg, sb = sum(rgb[0::3]), sum(rgb[1::3]), sum(rgb[2::3])
    return sr / n, sg / n, sb / n, luma(sr, sg, sb) / n


def parse_box(args):
    return tuple(int(v) for v in args[:4]) if len(args) >= 4 else None


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    cmd = argv[0]
    if cmd == 'stats':
        img = read_png(argv[1])
        box = parse_box(argv[2:])
        if box:
            img = crop(img, box)
        r, g, b, lum = stats(img)
        print(f'{argv[1]}: {img[0]}x{img[1]} mean R {r:.1f} G {g:.1f} B {b:.1f} luma {lum:.1f}')
    elif cmd == 'diff':
        a, b = read_png(argv[1]), read_png(argv[2])
        box = parse_box(argv[3:])
        if box:
            a, b = crop(a, box), crop(b, box)
        a, b = match(a, b)
        n = a[0] * a[1]
        da = [0, 0, 0]
        over = 0
        ra, rb = a[2], b[2]
        for i in range(0, n * 3, 3):
            d0, d1, d2 = abs(ra[i] - rb[i]), abs(ra[i + 1] - rb[i + 1]), abs(ra[i + 2] - rb[i + 2])
            da[0] += d0
            da[1] += d1
            da[2] += d2
            if abs(luma(ra[i], ra[i + 1], ra[i + 2]) - luma(rb[i], rb[i + 1], rb[i + 2])) > 16:
                over += 1
        print(f'{a[0]}x{a[1]}: mean |dR| {da[0] / n:.1f} |dG| {da[1] / n:.1f} |dB| {da[2] / n:.1f}  pixels with |dluma| > 16: {100.0 * over / n:.2f}%')
    elif cmd == 'grid':
        img = read_png(argv[1])
        cols, rows = int(argv[2]), int(argv[3])
        w, h, rgb = img
        cw, chh = w // cols, h // rows
        for r in range(rows):
            vals = []
            for c in range(cols):
                cell = crop(img, (c * cw, r * chh, cw, chh))
                vals.append(f'{stats(cell)[3]:5.1f}')
            print(' '.join(vals))
    elif cmd == 'hist':
        img = read_png(argv[1])
        box = parse_box(argv[2:])
        if box:
            img = crop(img, box)
        bins = [0] * 16
        rgb = img[2]
        for i in range(0, len(rgb), 3):
            bins[luma(rgb[i], rgb[i + 1], rgb[i + 2]) >> 4] += 1
        n = img[0] * img[1]
        print(' '.join(f'{100.0 * v / n:.1f}' for v in bins))
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
