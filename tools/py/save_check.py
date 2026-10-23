#!/usr/bin/env python
"""save_check.py <save.dat> [slot]: per-slot directory / title / checksum / preview verdict for card 1 (read-only)."""
import os
import struct

import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cc_save as C  # noqa: E402


try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:  # noqa: BLE001
    pass


def sjis(b):
    return b.split(b"\0")[0].decode("shift_jis", "replace")


def check_slot(card, e, blk):
    u32 = lambda o: struct.unpack_from("<I", blk, o)[0]
    out = []
    out.append("dir: idx %d state %#x size %#x next %#x name %s xor_ok=%s" % (e.idx, e.state, e.size, e.next, e.name, e.xor_ok))
    out.append("frame: magic %r icon_flag %#x blocks %#x title %s" % (blk[:2], blk[2], blk[3], sjis(blk[4:0x28])))
    out.append("preview: name %s party %s playtime@37 %d (%s) gold@3B %d stars@3F %d prog@40 %#x" % (
        sjis(blk[0x28:0x34]), list(blk[0x34:0x37]), u32(0x37), hms(u32(0x37)), u32(0x3B), blk[0x3F], blk[0x40]))
    out.append("state: playtime@588 %d (%s) gold@58C %d party@59C %s loc@18E %#x stars@5A4/5 %d/%d opts@5A0 %#x" % (
        u32(0x588), hms(u32(0x588)), u32(0x58C), list(blk[0x59C:0x59F]), struct.unpack_from("<H", blk, 0x18E)[0],
        blk[0x5A4], blk[0x5A5], struct.unpack_from("<H", blk, 0x5A0)[0]))
    out.append("trailer@1FF8 %s checksum stored %#x computed %#x -> %s" % (
        blk[0x1FF8:0x1FFC].hex(), u32(0x1FFC), C.block_sum(blk), "OK" if C.block_checksum_ok(blk) else "BAD"))
    ver = u32(0x1FF8)
    rde = sum(blk[0x180:0x1FF8]) + (sum(blk[:0x180]) if ver > 0x102 else 0)
    out.append("RDE load verdict (menutest: version %#x, sum %s header): %s" % (
        ver, "with" if ver > 0x102 else "without", "OK" if (rde & 0xFFFFFFFF) == u32(0x1FFC) else "DATA CORRUPTED"))
    out.append("summary bit count @24 %d (RDE writes 210), CRD0 marker @50 %s" % (u32(0x24), blk[0x50:0x56].hex()))
    out.append("tail nonzero bytes (1F21..1FF7): %d; packed nonzero: %d; head 0x100..0x5AB nonzero: %d" % (
        sum(1 for x in blk[0x1F21:0x1FF8] if x), sum(1 for x in blk[0x5AC:0x1F21] if x), sum(1 for x in blk[0x100:0x5AC] if x)))
    try:
        C.decompress(blk)
        out.append("packed stream decodes: yes")
    except Exception as ex:  # noqa: BLE001
        out.append("packed stream decodes: NO (%s)" % ex)
    return out


def check_ext(data, want=None):
    """ext_save.dat: header, per-slot record sanity, the pair table, RDE's load verdict per slot."""
    out = []
    ver = struct.unpack_from("<I", data, 4)[0] if len(data) >= 8 else 0
    out.append("ext_save.dat: %d bytes, magic %r, version %#x" % (len(data), data[:4], ver))
    if data[:4] != b"EXT\0":
        out.append("  BAD magic (RDE ignores the file and starts with defaults)")
        return out
    if not ((len(data) == 0xA634 and ver == 0x104) or (len(data) == 0xA5F8 and ver == 0x103)):
        out.append("  RDE ignores this file (needs 0xA634 B + version 0x104, or 0xA5F8 + 0x103): defaults are used")
        return out
    if len(data) == 0xA5F8:
        out.append("  legacy 0x103 file: RDE upgrades it on the next write")
    for s in range(30):
        o = 8 + s * 0x588
        lang, bc = struct.unpack_from("<II", data, o)
        names = []
        for k in range(44):
            raw = data[o + 8 + k * 32:o + 8 + k * 32 + 32]
            n = raw.index(b"\0") if b"\0" in raw else 32
            probs = []
            if n == 32:
                probs.append("no NUL terminator")
            if any(raw[n:]):
                probs.append("bytes after the NUL")
            try:
                t = raw[:n].decode("utf-8")
            except UnicodeDecodeError:
                t = raw[:n].decode("utf-8", "replace")
                probs.append("invalid UTF-8")
            if n:
                names.append((k, t, probs))
        pair = (data[0xA5F8 + 2 * s], data[0xA5F9 + 2 * s]) if len(data) == 0xA634 else (0, 0)
        if not (lang or bc or names or any(pair)):
            continue
        card, slot = s // 15 + 1, s % 15
        if want is not None and slot != want:
            continue
        issues = []
        if lang > 5:
            issues.append("language %d outside 0..5" % lang)
        if bc > 0xFFFF:
            issues.append("battle counter %d above the game's 0xFFFF cap" % bc)
        if pair[0] not in (0, 3, 4, 5, 6) or pair[1] > 1:
            issues.append("pair %s: RDE accepts the form byte 3..6 (anything else falls back to its story byte) and valid 0/1" % (pair,))
        for k, t, probs in names:
            if probs:
                issues.append("name %d %r: %s" % (k, t, ", ".join(probs)))
        out.append("slot index %d (card %d CHRONO%02d): language %d battles %d pair %s names %s -> RDE load: counter restored; names %s" % (
            s, card, slot, lang, bc, pair, [(k, t) for k, t, _ in names],
            "restored when the running language is %d" % lang if names else "none"))
        for i in issues:
            out.append("  PROBLEM: " + i)
    return out


def hms(f):
    s = f // 60
    return "%d:%02d:%02d" % (s // 3600, s // 60 % 60, s % 60)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    path = argv[1]
    want = int(argv[2]) if len(argv) > 2 else None
    card = C.load_card(path, 1)
    print("card1 header_ok=%s" % card.header_ok)
    for e in card.dir:
        if e.state == 0xA0 and want is None:
            continue
    slots = card.cc_slots()
    for n in sorted(slots):
        if want is not None and n != want:
            continue
        e, blk = slots[n]
        print("== slot %d (%s)" % (n, e.name))
        for l in check_slot(card, e, blk):
            print("  " + l)
    others = [e for e in card.dir if e.used and not e.first or (e.state not in (0xA0, 0x51))]
    for e in others:
        print("other dir frame %d: state %#x name %s" % (e.idx, e.state, e.name))
    if path.endswith(".dat"):
        try:
            for m in C.read_zip(path):
                if m.name == "ext_save.dat":
                    for l in check_ext(m.data, want):
                        print("  " + l)
                else:
                    print("member %s: %d bytes hmac_ok=%s" % (m.name, len(m.data), m.hmac_ok))
        except Exception as ex:  # noqa: BLE001
            print("members: %s" % ex)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
