"""Field-by-field diff of two game states in save terms (New Game+ / Continue+ checks against ccx).

    python tools/py/ngplus_diff.py A B [--all]

A / B: a ccx DuckStation state (.sav: guest RAM, read with ccx_state.py), a raw 0x2000 save block (.bin: OpenCross route
`request slotdump` with OC_SLOT_DUMP=<path>) or `<save.dat>:<slot>` (card 1). Every side becomes the saved view of the game:
  * the 0xE1F4 image (RAM 0x8006AEEC + X) passed through the slot packer, so only the saved bits are compared
    (character records, items, elements, names, unlock masks, equipped nibbles, 790A4.., button configs, 790CC/D0, frames, 790D8/DC);
  * the raw head fields: key items 0x80078020, script variables (ccx: the field's live block 0x800EBAA8; `--saved-vars` the global
    block 0x80077C20), play time 0x8007210C, gold 0x800786F0, recruited 0x800786E8, party 0x800786F4..F7, options 0x800789C0, stars
    0x800789BC / BE.
Prints one line per differing field (A value, B value). --all also lists equal fields of the carried set.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cc_save as c  # noqa: E402

IMG_BASE = 0x8006AEEC
STAT_NAMES = {0x01: "colour", 0x02: "rec02", 0x16: "max_hp", 0x18: "atk", 0x19: "def", 0x1A: "hit", 0x1B: "evd", 0x1C: "mag",
              0x1D: "mdef", 0x20: "weapon", 0x22: "armour", 0x24: "acc1", 0x26: "acc2", 0x28: "acc3", 0x2D: "model", 0xA8: "rec_a8",
              0xB0: "status", 0xB8: "cur_hp", 0xBC: "hp_snap", 0xBE: "hp_pending"}
BONUS = ["atk", "hit", "def", "evd", "mag", "mdef"]


def masked(img):
    blk = bytearray(0x2000)
    blk[c.PACK_OFF:c.PACK_END] = c.compress(bytes(img))
    return c.decompress(blk)


class View:
    def __init__(self, spec, saved_vars=False):
        self.name = spec
        self.head = {}
        if spec.lower().endswith(".sav"):
            import ccx_state
            st = ccx_state.State(spec)
            ram = st.ram
            r = lambda a, n: bytes(ram[(a & 0x1FFFFFFF):(a & 0x1FFFFFFF) + n])  # noqa: E731
            self.img = masked(r(IMG_BASE, c.IMG_SIZE))
            self.keys = r(0x80078020, 8)
            self.vars = r(0x80077C20 if saved_vars else 0x800EBAA8, 0x400)
            self.playtime = struct.unpack("<I", r(0x8007210C, 4))[0]
            self.gold = struct.unpack("<I", r(0x800786F0, 4))[0]
            self.recruited = int.from_bytes(r(0x800786E8, 6), "little") & ((1 << 44) - 1)
            self.party = tuple(r(0x800786F4, 4))
            self.options = struct.unpack("<H", r(0x800789C0, 2))[0]
            self.stars = (r(0x800789BC, 1)[0], r(0x800789BE, 1)[0])
            return
        if ":" in os.path.basename(spec):
            path, slot = spec.rsplit(":", 1)
            card = c.load_card(path, 1)
            blk = card.cc_slots()[int(slot)][1]
        else:
            blk = open(spec, "rb").read()
        if len(blk) != 0x2000:
            raise SystemExit("%s: not a 0x2000 block" % spec)
        self.img = c.decompress(blk)
        self.keys = bytes(blk[0x180:0x188])
        self.vars = bytes(blk[0x188:0x588])
        self.playtime = struct.unpack_from("<I", blk, 0x588)[0]
        self.gold = struct.unpack_from("<I", blk, 0x58C)[0]
        self.recruited = int.from_bytes(blk[0x590:0x596], "little") & ((1 << 44) - 1)
        self.party = tuple(blk[0x59C:0x5A0])
        self.options = struct.unpack_from("<H", blk, 0x5A0)[0]
        self.stars = (blk[0x5A4], blk[0x5A5])


def img_field(off):
    """Name and width of the saved field at image offset `off` (None = not a field start)."""
    if 0xAA24 <= off < 0xAA24 + 44 * 0xCC:
        ch, ro = divmod(off - 0xAA24, 0xCC)
        if 0x40 <= ro < 0xA0:
            return "char%02d.grid[%d]" % (ch, (ro - 0x40) // 2), 2, ro % 2 == 0
        if 0xC0 <= ro < 0xCC:
            k, b = divmod(ro - 0xC0, 2)
            return "char%02d.%s_%s" % (ch, BONUS[k], "pending" if b else "snap"), 1, True
        nm = STAT_NAMES.get(ro) or STAT_NAMES.get(ro - 1)
        w = 2 if ro in (0x16, 0x20, 0x22, 0x24, 0x26, 0x28, 0xB8, 0xBC, 0xBE) else 1
        return "char%02d.%s" % (ch, STAT_NAMES.get(ro, "+%02X" % ro)), w, ro in STAT_NAMES
    if 0xD13C <= off < 0xD43C:
        return "item_id[%d]" % ((off - 0xD13C) // 2), 2, (off - 0xD13C) % 2 == 0
    if 0xD43C <= off < 0xD5BC:
        return "item_qty[%d]" % (off - 0xD43C), 1, True
    if 0xD5BC <= off < 0xD73C:
        return "elem_id[%d]" % ((off - 0xD5BC) // 2), 2, (off - 0xD5BC) % 2 == 0
    if 0xD73C <= off < 0xD7FC:
        return "elem_qty[%d]" % (off - 0xD73C), 1, True
    if 0xD80C <= off < 0xD80C + 44 * 16:
        return "name%02d" % ((off - 0xD80C) // 16), 16, (off - 0xD80C) % 16 == 0
    if 0xDAD8 <= off < 0xDAD8 + 44 * 16:
        ch, k = divmod(off - 0xDAD8, 16)
        return "char%02d.unlock[%d]" % (ch, k // 2), 2, k % 2 == 0
    if 0xDD98 <= off < 0xDD98 + 44 * 0x18:
        ch, k = divmod(off - 0xDD98, 0x18)
        return "char%02d.eq_nibbles[%d]" % (ch, k), 1, True
    words = {0x0000: "btn_field", 0x0004: "btn_battle", 0x0008: "btn_field_an", 0x000C: "btn_battle_an", 0xE1B8: "lock_790A4",
             0xE1BC: "lock_790A8", 0xE1C0: "excl_790AC", 0xE1C4: "excl_790B0", 0xE1E0: "w790CC", 0xE1E4: "w790D0", 0xE1E8: "frames_790D4",
             0xE1EC: "carry_790D8", 0xE1F0: "carry_790DC"}
    if off in words:
        return words[off], 4 if off != 0xE1E8 else 2, True
    return "img+%04X" % off, 1, True


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    sys.stdout.reconfigure(encoding="utf-8")
    show_all = "--all" in argv
    saved_vars = "--saved-vars" in argv
    a, b = (View(x, saved_vars) for x in [p for p in argv if not p.startswith("--")][:2])
    diffs = 0

    def rep(name, va, vb):
        nonlocal diffs
        if va != vb:
            diffs += 1
            print("DIFF %-28s A=%s  B=%s" % (name, va, vb))
        elif show_all:
            print("same %-28s %s" % (name, va))

    off = 0
    seen = set()
    while off < c.IMG_SIZE:
        name, w, start = img_field(off)
        if off < 0x10:  # the button configs: the pack reads them from the pad tables, not from this RAM window
            off += 4
            continue
        if not start or name in seen:
            off += 1
            continue
        seen.add(name)
        va, vb = bytes(a.img[off:off + w]), bytes(b.img[off:off + w])
        if va != vb or show_all:
            if name.startswith("name"):
                fa, fb = c.decode_text(va), c.decode_text(vb)
            else:
                fa, fb = int.from_bytes(va, "little"), int.from_bytes(vb, "little")
                if w >= 2:
                    fa, fb = "%#x" % fa, "%#x" % fb
            rep(name, fa, fb)
        off += w
    rep("key_items", a.keys.hex(), b.keys.hex())
    for vid in range(0, 0x400, 2):
        va = struct.unpack_from("<H", a.vars, vid)[0]
        vb = struct.unpack_from("<H", b.vars, vid)[0]
        rep("var %03X" % vid, "%#x" % va, "%#x" % vb)
    rep("playtime", a.playtime, b.playtime)
    rep("gold", a.gold, b.gold)
    rep("recruited", "%011x" % a.recruited, "%011x" % b.recruited)
    rep("party+relief", a.party, b.party)
    rep("options", "%#x" % a.options, "%#x" % b.options)
    rep("stars total/current", a.stars, b.stars)
    print("%d differing fields (A=%s, B=%s)" % (diffs, a.name, b.name))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
