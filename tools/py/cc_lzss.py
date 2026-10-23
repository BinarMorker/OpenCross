"""Chrono Cross 'sszl' LZSS decoder.


Layout [C]:
  +0  "sszl"
  +4  u32 decoded length (see decode() for the one known caveat)
  +8  u32 unknown (not needed for decoding)
  +12 bit stream, bits read MSB-first from each byte:
        1 + 8 bits               -> literal byte
        0 + 12 bits pos + 4 bits -> copy (count+2) bytes from the 4 KiB ring
  The ring is zero initialised and the write pointer starts at 1 [C: derived
  from data - this is the only start value for which every room's
  background-assembly table is monotonic; positions are absolute ring indices].
"""
import struct

MAGIC = b"sszl"
RING_SIZE = 4096
RING_START = 1


def is_sszl(data):
    return len(data) >= 12 and data[:4] == MAGIC


def declared_length(data):
    return struct.unpack_from("<I", data, 4)[0]


def decode(data, length=None):
    """Decode an sszl blob. Returns bytes of the header's decoded length (or
    `length` if given). Stops early (returns fewer bytes) if the stream ends."""
    if not is_sszl(data):
        raise ValueError("not an sszl stream")
    want = declared_length(data) if length is None else length
    body = data[12:]
    nbits = len(body) * 8
    stream = int.from_bytes(body, "big")      # MSB-first bit reader
    ring = bytearray(RING_SIZE)
    wp = RING_START
    out = bytearray()
    bp = 0

    def take(k):
        nonlocal bp
        v = (stream >> (nbits - bp - k)) & ((1 << k) - 1)
        bp += k
        return v

    while len(out) < want:
        if bp + 9 > nbits:
            break
        if take(1):
            c = take(8)
            out.append(c)
            ring[wp] = c
            wp = (wp + 1) & 0xFFF
        else:
            if bp + 16 > nbits:
                break
            p = take(12)
            n = take(4) + 2
            for _ in range(n):
                c = ring[p]
                p = (p + 1) & 0xFFF
                out.append(c)
                ring[wp] = c
                wp = (wp + 1) & 0xFFF
    del out[want:]
    return bytes(out)
