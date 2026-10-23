"""ccx_pc.py <state.sav|dir> ... : the CPU registers of a ccx state (DuckStation CPU section: 'CPU\0' tag at body 42, r0 at body offset 45,
r[0..31], then 'hi lo pc npc'-like words; the program counter sampled on a hung state = where the game waits). Prints ra sp and the next 4 words as hex."""
import os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ccx_state as c, zstandard


def regs(path):
    raw = open(path, "rb").read()
    fr = []
    q = raw.find(c.ZSTD_MAGIC)
    while q != -1 and len(fr) < 4:
        fr.append(q)
        q = raw.find(c.ZSTD_MAGIC, q + 1)
    body = zstandard.ZstdDecompressor().decompressobj().decompress(raw[fr[1]:]) if len(fr) >= 2 else raw
    t = body.find(b"CPU\x00", 0, 200)
    w = struct.unpack_from("<40I", body, t + 3 + 0)
    return w


if __name__ == "__main__":
    for a in sys.argv[1:]:
        ps = [os.path.join(a, f) for f in sorted(os.listdir(a))] if os.path.isdir(a) else [a]
        for p in ps:
            w = regs(p)
            print(os.path.basename(p), "r0", hex(w[2]), "gp", hex(w[30]), "sp", hex(w[31]), "ra", hex(w[33]), "w34..39", [hex(x) for x in w[34:40]])
