"""Test-save helper: copy a save.dat with key items added to one slot (route fixtures; never point it at the owner's folder).

    python tools/py/cc_save_keyitem.py <in save.dat> <out save.dat> <slot> <key item> [<key item> ...]

Key item n (1..64) = bit n-1 of the raw head 0x180..0x187. The block checksum (0x1FFC) is recomputed. The
output is a plain (unencrypted, stored) ZIP with the same members, which OpenCross reads like RDE's WinZip-AES file.
"""
import struct
import sys
import zipfile

import cc_save as c


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 1
    src, dst, slot, items = argv[0], argv[1], int(argv[2]), [int(a) for a in argv[3:]]
    members = c.read_zip(src)
    done = False
    for m in members:
        if m.name != "memcard1.mcr":
            continue
        card = c.Card(m.data)
        slots = card.cc_slots()
        if slot not in slots:
            raise SystemExit("slot %d not on card 1" % slot)
        entry, _ = slots[slot]
        data = bytearray(m.data)
        base = c.BLOCK * entry.idx
        blk = bytearray(data[base:base + c.BLOCK])
        bits = struct.unpack_from("<Q", blk, 0x180)[0]
        for n in items:
            bits |= 1 << (n - 1)
        struct.pack_into("<Q", blk, 0x180, bits)
        struct.pack_into("<I", blk, 0x1FFC, c.block_sum(blk))
        data[base:base + c.BLOCK] = blk
        m.data = bytes(data)
        done = True
    if not done:
        raise SystemExit("no memcard1.mcr in %s" % src)
    with zipfile.ZipFile(dst, "w", zipfile.ZIP_STORED) as z:
        for m in members:
            z.writestr(m.name, m.data)
    print("wrote %s: slot %d + key items %s" % (dst, slot, items))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
