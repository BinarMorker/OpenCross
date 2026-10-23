"""PS1 TIM decoder + 1024x512 16-bit VRAM emulation + minimal PNG writer.

Classic TIM: u32 0x10, u32 flags (bits0-1 = 0:4bpp 1:8bpp 2:16bpp 3:24bpp, bit3 = has CLUT);
[CLUT block: u32 len, u16 x, u16 y, u16 w, u16 h, w*h u16 entries]; image block:
u32 len, u16 x, u16 y, u16 w (in 16-bit halfwords), u16 h, w*h u16.
Room background pages have flags=1 (8bpp, no CLUT; one 64x256-halfword page = 128x256 texels);
model/NPC textures have flags=9 (8bpp + 256-entry CLUT).  The blit works on raw halfwords.
"""
import struct
import zlib
from array import array

VRAM_W, VRAM_H = 1024, 512


class Tim:
    def __init__(self):
        self.bpp = 0
        self.has_clut = False
        self.clut = None        # (x, y, w, h, list[int])
        self.image = None       # (x, y, w_halfwords, h, array('H'))


def parse_tim(data):
    if len(data) < 20 or struct.unpack_from("<I", data, 0)[0] != 0x10:
        raise ValueError("not a TIM")
    flags = struct.unpack_from("<I", data, 4)[0]
    t = Tim()
    t.bpp = (4, 8, 16, 24)[flags & 3]
    t.has_clut = bool(flags & 8)
    off = 8
    if t.has_clut:
        ln, x, y, w, h = struct.unpack_from("<IHHHH", data, off)
        ent = array("H")
        ent.frombytes(data[off + 12:off + 12 + w * h * 2])
        t.clut = (x, y, w, h, ent)
        off += ln
    ln, x, y, w, h = struct.unpack_from("<IHHHH", data, off)
    px = array("H")
    px.frombytes(data[off + 12:off + 12 + w * h * 2])
    if len(px) != w * h:
        raise ValueError("TIM image truncated (%d of %d halfwords)" % (len(px), w * h))
    t.image = (x, y, w, h, px)
    return t


def bgr555_to_rgba(c):
    """PS1 15-bit colour -> (r,g,b,a). a=0 for c==0 (transparent black); STP bit ignored."""
    r = (c & 31) << 3
    g = ((c >> 5) & 31) << 3
    b = ((c >> 10) & 31) << 3
    return (r | r >> 5, g | g >> 5, b | b >> 5, 0 if c == 0 else 255)


class Vram:
    """1024x512 x 16-bit video memory. Coordinates are in halfwords."""

    def __init__(self):
        self.px = array("H", bytes(VRAM_W * VRAM_H * 2))

    def blit(self, x, y, w, h, data):
        for r in range(h):
            yy = (y + r) % VRAM_H
            xx = x % VRAM_W
            n = min(w, VRAM_W - xx)
            self.px[yy * VRAM_W + xx:yy * VRAM_W + xx + n] = data[r * w:r * w + n]
            if n < w:                              # horizontal wrap
                self.px[yy * VRAM_W:yy * VRAM_W + (w - n)] = data[r * w + n:(r + 1) * w]

    def blit_tim(self, tim, with_clut=True):
        if tim.has_clut and with_clut:
            x, y, w, h, e = tim.clut
            self.blit(x, y, w, h, e)
        x, y, w, h, p = tim.image
        self.blit(x, y, w, h, p)

    def write_bytes(self, byte_offset, data):
        """Copy raw bytes to VRAM at a byte offset (row stride 2048 bytes)."""
        for i in range(0, len(data), 2048):
            chunk = data[i:i + 2048]
            a = array("H")
            a.frombytes(chunk[:len(chunk) & ~1])
            o = (byte_offset + i) // 2
            self.px[o:o + len(a)] = a

    def clut256(self, x, y):
        return self.px[y * VRAM_W + x:y * VRAM_W + x + 256]

    def texel8(self, hx, y, k):
        """8bpp texel k (0 = low byte) of halfword column hx at row y."""
        v = self.px[y * VRAM_W + hx]
        return (v >> (8 * k)) & 0xFF if k < 2 else None

    def row8(self, hx, y, npix):
        """npix 8bpp indices starting at halfword hx (little-endian: low byte first)."""
        base = y * VRAM_W + hx
        out = bytearray()
        for i in range((npix + 1) // 2):
            v = self.px[base + i]
            out.append(v & 255)
            out.append(v >> 8)
        return bytes(out[:npix])


def write_png(path, width, height, rgba):
    """rgba: bytes/bytearray of width*height*4. Standard-library PNG (RGBA8)."""
    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw += rgba[y * stride:(y + 1) * stride]

    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)
