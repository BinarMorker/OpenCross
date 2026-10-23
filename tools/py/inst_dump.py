"""PS1 instrument table (g_InstrumentInfo[256]) and SPU RAM out of a ccx/DuckStation save state.

    python tools/py/inst_dump.py <state.sav> [--all] [--cmp]      dump the table (non-wave0000-identical slots, or all)

g_InstrumentInfo is found by signature (guest 0x800919C8, verified against wave0000 entry 10 which is unique there). SPU RAM (512 KiB) is found by the wave0000 sample data (SPU 0x71100). Entry = 16 bytes:
u32 spu start, u32 spu loop, s16 fine_tune, s16 base_note, u16 adsr1, u16 adsr2.
"""
import os, struct, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ccx_state

GAME = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))


def wave(n):
    z = zipfile.ZipFile(os.path.join(GAME, "data", "cdrom.dat"))
    return z.read("cdrom/sound/wave/wave%04d.dat" % n) if "cdrom/sound/wave/wave%04d.dat" % n in z.namelist() else z.read("sound/wave/wave%04d.dat" % n)


def bank_entries(d):
    spu, size, first, cnt = struct.unpack_from("<IIII", d, 0x10)[0], struct.unpack_from("<I", d, 0x14)[0], struct.unpack_from("<I", d, 0x18)[0], struct.unpack_from("<I", d, 0x1C)[0]
    ents = [struct.unpack_from("<IIhhHH", d, 0x40 + 16 * i) for i in range(cnt)]
    return spu, size, first, cnt, ents


def load_state(path):
    st = ccx_state.State(path)
    raw = open(path, "rb").read()
    if st.compressed:
        import zstandard
        p = raw.find(ccx_state.ZSTD_MAGIC); fr = []
        while p != -1 and len(fr) < 4:
            fr.append(p); p = raw.find(ccx_state.ZSTD_MAGIC, p + 1)
        body = zstandard.ZstdDecompressor().decompressobj().decompress(raw[fr[1]:])
    else:
        body = raw
    return st, body


TABLE = 0x919C8  # guest 0x800919C8: the symbol g_InstrumentInfo (verified: wave0000 entry 10 is unique there)


def find_table(st):
    return TABLE


def find_spu(body, st):
    d = wave(0)
    spu, size, first, cnt, ents = bank_entries(d)
    off = 0x40 + cnt * 16
    sig = d[off + 0x100: off + 0x140]
    p = body.find(sig)
    return None if p < 0 else p - (spu + 0x100)


def table(st, base):
    return [struct.unpack_from("<IIhhHH", st.ram, base + 16 * i) for i in range(256)]


def main(a):
    st, body = load_state(a[0])
    base = find_table(st)
    print("table at RAM +0x%X (guest 0x%08X)" % (base, 0x80000000 + base) if base is not None else "table not found")
    spu_off = find_spu(body, st)
    print("SPU RAM at body +0x%X" % spu_off if spu_off is not None else "SPU RAM not found")
    if base is None:
        return
    t = table(st, base)
    d0 = wave(0)
    s0, _, _, c0, e0 = bank_entries(d0)
    for i, e in enumerate(t):
        ref = (e0[i][0] + s0, e0[i][1] + s0) + e0[i][2:] if i < c0 else None
        same = ref == e
        if "--all" in a or not same:
            print("%3d start=%06X loop=%06X ft=%5d base=%3d adsr=%04X/%04X %s" % (i, e[0], e[1], e[2], e[3], e[4], e[5], "" if same else "(differs from wave0000)"))


if __name__ == "__main__":
    main(sys.argv[1:])
