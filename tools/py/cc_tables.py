"""cc_tables - decoders/dumpers for Chrono Cross (RDE) battle and menu data tables.

Original code.  Standard library only.
Run with:  python -I tools/py/cc_tables.py <command> [--game DIR] [-o FILE]

Commands: identify enemies elements groups drops growth chars keyitems shops gear bscript
"""
import csv
import io
import os
import re
import struct
import sys
import zipfile

DEFAULT_GAME = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
_WT = os.sep + os.path.join(".claude", "worktrees") + os.sep
if _WT in DEFAULT_GAME + os.sep:  # run from a .claude/worktrees/<x> checkout -> the game dir is above the main checkout
    DEFAULT_GAME = os.path.dirname((DEFAULT_GAME + os.sep).split(_WT)[0])

# --------------------------------------------------------------------------
# container helpers (private; cc_lzss/cc_container may exist but are not required)
# --------------------------------------------------------------------------

def lzss_decode(data):
    """CC 'sszl' LZSS: 12-byte header (magic, u32 decoded size, u32 aux), MSB-first
    bit stream; 1 + 8-bit literal, 0 + 12-bit ring position + 4-bit count (copy count+2).
    4 KB zero ring; the ring write pointer STARTS AT 1 (verified: el_mask/menu_pg/
    menu_waa/lv99para decode byte-identical to the plain battle/data files)."""
    if data[:4] != b"sszl":
        raise ValueError("not sszl")
    want = struct.unpack_from("<I", data, 4)[0]
    ring = bytearray(4096)
    wp = 1
    out = bytearray()
    acc = 0
    nb = 0
    pos = 12
    n = len(data)

    def take(k):
        nonlocal acc, nb, pos
        while nb < k:
            if pos >= n:
                return None
            acc = (acc << 8) | data[pos]
            pos += 1
            nb += 8
        nb -= k
        v = (acc >> nb) & ((1 << k) - 1)
        acc &= (1 << nb) - 1
        return v

    while len(out) < want:
        f = take(1)
        if f is None:
            break
        if f:
            c = take(8)
            if c is None:
                break
            out.append(c)
            ring[wp] = c
            wp = (wp + 1) & 4095
        else:
            p = take(12)
            cn = take(4)
            if cn is None:
                break
            for _ in range(cn + 2):
                c = ring[p]
                p = (p + 1) & 4095
                out.append(c)
                ring[wp] = c
                wp = (wp + 1) & 4095
    return bytes(out[:want])


def cpt_split(d):
    """cpt = u32 count, u32 offsets[count], u32 end.  Returns list of bytes or None."""
    if len(d) < 12:
        return None
    n = struct.unpack_from("<I", d, 0)[0]
    if n == 0 or n > 4096 or 8 + 4 * n > len(d):
        return None
    o = struct.unpack_from("<%dI" % (n + 1), d, 4)
    if o[0] != 8 + 4 * n:
        return None
    if any(o[i] > o[i + 1] for i in range(n)) or o[n] > len(d):
        return None
    return [d[o[i]:o[i + 1]] for i in range(n)]


class Game:
    def __init__(self, root=None):
        self.root = os.path.abspath(root or DEFAULT_GAME)
        self.cd = zipfile.ZipFile(os.path.join(self.root, "data", "cdrom.dat"))
        self._lang = None
        self._cache = {}

    def read(self, name):
        return self.cd.read(name)

    def names(self, prefix=""):
        return [n for n in self.cd.namelist() if n.startswith(prefix) and not n.endswith("/")]

    def lang(self):
        if self._lang is None:
            self._lang = zipfile.ZipFile(os.path.join(self.root, "data", "lang.dat"))
        return self._lang

    def tsv(self, member):
        """language-column TSV (UTF-16LE BOM): returns dict key-string -> [JP, EN, FR, IT, DE, ES]."""
        if member in self._cache:
            return self._cache[member]
        raw = self.lang().read(member)
        if raw[:2] == b"\xff\xfe":
            txt = raw[2:].decode("utf-16-le", "replace")
        else:
            txt = raw.decode("utf-8-sig", "replace")
        rows = csv.reader(io.StringIO(txt, newline=""), delimiter="\t", quotechar='"')
        out = {}
        for i, r in enumerate(rows):
            if i == 0 or not r:
                continue
            out[r[0].strip()] = r[1:]
        self._cache[member] = out
        return out

    def sysmes(self, cat, idx, lang=2):
        """sysmes.txt string: key hex 0xCCIIII.  lang column: 0/1=JP 2=EN 3=FR 4=IT 5=DE 6=ES (col 0 is the "Text" column = JP)."""
        t = self.tsv("sysmes.txt")
        for k in ("0x%02X%04X" % (cat, idx), "0x%06X" % ((cat << 16) | idx), "%06X" % ((cat << 16) | idx)):
            v = t.get(k)
            if v is None:
                v = t.get(k.lower())
            if v is not None and len(v) > lang:
                return v[lang].replace("\r", " ").replace("\n", " ")
        return ""


def u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def load_bundle(g, name):
    """Return the list of decoded sub-blobs of a cpt bundle (player.dat, exdat.dat, eqp.dat)."""
    subs = cpt_split(g.read(name))
    return [lzss_decode(s) if s[:4] == b"sszl" else s for s in subs]


# --------------------------------------------------------------------------
# constants
# --------------------------------------------------------------------------

COLOUR_BITS = {0x04: "green", 0x08: "white", 0x10: "black", 0x20: "red", 0x40: "yellow", 0x80: "blue"}
MONSTER_TYPE_BITS = {0: "floating", 1: "undead", 2: "soft", 3: "inorganic", 4: "aquatic?", 13: "dragon",
                     14: "human", 15: "demon"}
REC = 0xB0           # enemy / character stat record size (monster.bin, char*.bin)
ELEM_REC = 0x2C      # element record size (pgdat.bin)


def colour_name(b):
    return COLOUR_BITS.get(b, "none" if b == 0 else "0x%02X" % b)


def item_name(g, v):
    """name of an item-space id as used in drop tables (bit15 -> element/consumable)."""
    if v == 0:
        return ""
    if v & 0x8000:
        return "E:" + g.sysmes(0x17, v & 0x7FFF)
    return "I:" + g.sysmes(0, v)


# --------------------------------------------------------------------------
# table loaders
# --------------------------------------------------------------------------

def load_monsters(g):
    d = g.read("battle/data/monster.bin")
    return [d[i * REC:(i + 1) * REC] for i in range(len(d) // REC)]


def load_chars(g, name="char.bin"):
    d = g.read("battle/data/" + name)
    n = 51
    recs = [d[i * REC:(i + 1) * REC] for i in range(n)]
    lv99 = d[n * REC:] if len(d) > n * REC else g.read("battle/data/lv99para.bin")
    return recs, lv99


def load_elements(g):
    d = g.read("battle/data/pgdat.bin")
    return [d[i * ELEM_REC:(i + 1) * ELEM_REC] for i in range(len(d) // ELEM_REC)]


def load_scenes(g):
    s = load_bundle(g, "battle/data/player.dat")[0]
    return [s[i * 28:(i + 1) * 28] for i in range(len(s) // 28)]


def load_symtbl(g):
    s = load_bundle(g, "battle/data/player.dat")[2]
    return list(struct.unpack("<%dH" % (len(s) // 2), s))


def load_ditem(g):
    d = lzss_decode(g.read("battle/data/ditem.dat"))
    return [struct.unpack_from("<4H", d, i * 8) for i in range(len(d) // 8)]


def load_growth(g):
    d = g.read("battle/data/el_mask.bin")
    return [d[i * 8:(i + 1) * 8] for i in range(len(d) // 8)]


def grid_cells(rec, off=0x40, n=48):
    out = []
    for i in range(n):
        v = u16(rec, off + 2 * i)
        out.append(v)
    return out


# --------------------------------------------------------------------------
# CSV helpers
# --------------------------------------------------------------------------

def emit(rows, header, out):
    w = csv.writer(out, lineterminator="\n")
    w.writerow(header)
    for r in rows:
        w.writerow(r)


def type_names(bits):
    return "|".join(n for b, n in sorted(MONSTER_TYPE_BITS.items()) if bits >> b & 1)


def hx(b):
    return b.hex()


# --------------------------------------------------------------------------
# commands: enemies / chars
# --------------------------------------------------------------------------

def stat_row(g, i, r, name):
    ty = (r[0xAE] << 8) | r[0xAF]
    return [i, name, "0x%02X" % r[1], colour_name(r[1]), "0x%02X" % r[0x15], u16(r, 0x16), r[0x18], r[0x19], r[0x1A],
            r[0x1B], r[0x1C], r[0x1D], r[0x1E], "0x%X" % u16(r, 0x20), "0x%X" % u16(r, 0x22),
            "0x%X" % u16(r, 0x24), "0x%X" % u16(r, 0x26), "0x%X" % u16(r, 0x28), r[0x2B], r[0x2D], r[0x2E], r[0x2F],
            u16(r, 0xA0), u16(r, 0xA2), r[0xA4], r[0xA5], "0x%02X" % r[0xA6], "0x%02X" % r[0xA7], "0x%04X" % ty,
            type_names(ty), hx(r[0x30:0x40]), hx(r[0x40:0xA0])]


STAT_HEADER = ["id", "name", "innate", "innate_colour", "gender", "hp", "atk", "def", "hit", "evd", "mag", "mdef",
               "b1E", "weapon", "armour", "acc0", "acc1", "acc2", "doppel_id", "model", "ap", "weapon_model",
               "drop_id", "gold", "hit_med", "hit_heavy", "a6", "a7", "type_bits", "types", "level_masks_hex",
               "grid_hex"]


def cmd_enemies(g, a, out):
    rows = []
    for i, r in enumerate(load_monsters(g)):
        rows.append(stat_row(g, i, r, g.sysmes(0x19, i)))
    emit(rows, STAT_HEADER, out)


def cmd_chars(g, a, out):
    recs, lv99 = load_chars(g, a.file)
    hdr = STAT_HEADER + ["growth_col", "lv99_hp", "lv99_atk", "lv99_def", "lv99_hit", "lv99_evd", "lv99_mag",
                         "lv99_mdef"]
    rows = []
    for i, r in enumerate(recs):
        row = stat_row(g, i, r, g.sysmes(1, i))
        l = lv99[i * 8:(i + 1) * 8]
        if len(l) == 8:
            row += [r[3], u16(l, 0)] + list(l[2:8])
        else:
            row += [r[3]]
        rows.append(row)
    emit(rows, hdr, out)


def cmd_growth(g, a, out):
    rows = []
    for lv, r in enumerate(load_growth(g)):
        cells = []
        for b in r:
            cells.append("" if b == 0xFF else "0x%02X" % b)
        rows.append([lv] + cells)
    emit(rows, ["star_level"] + ["col%d" % c for c in range(8)], out)


# --------------------------------------------------------------------------
# elements / groups / drops / key items / shops
# --------------------------------------------------------------------------

def cmd_elements(g, a, out):
    rows = []
    for i, r in enumerate(load_elements(g)):
        rows.append([i, g.sysmes(0x17, i), hx(r[0:4]), "0x%02X" % r[4], "0x%02X" % r[5], colour_name(r[5]),
                     r[6], r[7], r[8], r[9], r[0xA], r[0xB], r[0xC], r[0xD], "0x%04X" % u16(r, 0x10),
                     "0x%04X" % u16(r, 0x12), hx(r[0x14:0x18]), "0x%08X" % u32(r, 0x18), "0x%08X" % u32(r, 0x1C),
                     hx(r[0x20:0x2C]), hx(r)])
    emit(rows, ["id", "name", "b0_3", "b4", "colour_bits", "colour", "level", "mode", "power", "b9", "status_pct",
                "bB", "variance", "bD", "effect_id", "w12", "w14_hex", "status_mask", "w1C", "tail_hex", "raw_hex"],
         out)


def cmd_groups(g, a, out):
    sc = load_scenes(g)
    rows = []
    for i, r in enumerate(sc):
        mons = []
        for j in range(3):
            if r[j] != 0xFF:
                mons.append("%d:%s x%d" % (r[j], g.sysmes(0x19, r[j]), r[3 + j]))
        pos = " ".join("%02x" % b for b in r[9:15] if b != 0x11)
        rows.append([i, "evt%03d.pac" % i, "; ".join(mons), r[0], r[1], r[2], r[3], r[4], r[5], hx(r[6:9]), pos,
                     "0x%02X" % r[0xF], r[0x10], r[0x1A], r[0x1B], hx(r[0xB + 4:0x1C]), hx(r)])
    emit(rows, ["scene", "event_file", "monsters", "m0", "m1", "m2", "n0", "n1", "n2", "pc_pos_hex",
                "enemy_pos_hex", "b0F", "b10", "b1A_bg", "b1B", "tail_hex", "raw_hex"], out)
    if a.symtbl:
        out.write("\n# symtbl: encounter id -> scene index for 8 colour variants (value>=0x200: id-0x200, no variant roll)\n")
        st = load_symtbl(g)
        w = csv.writer(out, lineterminator="\n")
        w.writerow(["encounter"] + ["v%d" % k for k in range(8)])
        for e in range(len(st) // 8):
            w.writerow([e] + st[e * 8:e * 8 + 8])


def cmd_drops(g, a, out):
    mons = load_monsters(g)
    users = {}
    for i, r in enumerate(mons):
        users.setdefault(u16(r, 0xA0), []).append(i)
    rows = []
    for i, e in enumerate(load_ditem(g)):
        names = [item_name(g, v) for v in e]
        rows.append([i] + ["0x%04X" % v for v in e] + names +
                    ["; ".join("%d:%s" % (m, g.sysmes(0x19, m)) for m in users.get(i, []))])
    emit(rows, ["drop_id", "s0", "s1", "s2", "s3", "n0", "n1", "n2", "n3", "used_by_enemies"], out)


def cmd_keyitems(g, a, out):
    kd = g.read("menu/keyitem.dat")
    n_ptr = u16(kd, 0) // 2 - 1
    kt = g.tsv("keyitem.txt")
    rows = []
    for i in range(0, 65):
        name = g.sysmes(0x18, i)
        descs = []
        for k in range(3):
            v = kt.get(str(3 * i + k))
            descs.append((v[2] if v and len(v) > 2 else "").replace("\r", "").replace("\n", " "))
        rows.append([i, "0x%X" % (0x400 + i), name] + descs)
    emit(rows, ["key_item", "item_space_id", "name_en", "desc_row0", "desc_row1", "desc_row2"], out)
    out.write("# keyitem.dat: %d u16 offsets (%d description strings, JP PS1 text)\n" % (n_ptr, n_ptr))


def _rows64(d, n):
    return [[x for x in struct.unpack_from("<32H", d, i * 64)] for i in range(n)]


def trim(row):
    r = list(row)
    while r and r[-1] == 0:
        r.pop()
    return r


def load_gear(g):
    """256 x 0x1C equipment stat records (player.dat part 4 'waa.bin'), index = item id."""
    d = load_bundle(g, "battle/data/player.dat")[4]
    return [d[i * 28:(i + 1) * 28] for i in range(256)]


def load_item_info(g):
    """256 x 12 item info (exdat part 5 'menu_waa.bin'): equip mask u64, price/10 s16, cat, extra."""
    d = load_bundle(g, "battle/data/exdat.dat")[5]
    return [struct.unpack_from("<QhBB", d, i * 12) for i in range(256)]


GEAR_KIND = {0: "weapon", 1: "armour", 2: "accessory", 4: "material"}


def cmd_gear(g, a, out):
    gear = load_gear(g)
    info = load_item_info(g)
    w = csv.writer(out, lineterminator="\n")
    w.writerow(["id", "name", "kind", "tier", "class", "mode", "colour", "power", "mag", "acc_evd", "var", "flags",
                "slot", "mask_lo", "mask_hi", "status_lo", "status_hi", "price", "cat", "equip_mask", "forge"])
    forge = g.read("menu/waa_tbl.bin")
    for i in range(1, 256):
        r = gear[i]
        eq, p10, cat, ex = info[i]
        if not any(r) and not eq and not p10:
            continue
        f = struct.unpack_from("<H", r, 8)[0]
        rec = forge[i * 32:(i + 1) * 32]
        fg = " ".join("%d:%d" % (m, q) for m, q in enumerate(rec) if q)
        w.writerow([
            "0x%02X" % i, g.sysmes(0, i), GEAR_KIND.get(cat & 15, "?%d" % (cat & 15)), r[0], r[1], "0x%02X" % r[2],
            "0x%02X" % r[3], r[4], r[5], r[6], r[7], "0x%04X" % f, "0x%04X" % struct.unpack_from("<H", r, 10)[0],
            "0x%08X" % struct.unpack_from("<I", r, 12)[0], "0x%08X" % struct.unpack_from("<I", r, 16)[0],
            "0x%08X" % struct.unpack_from("<I", r, 20)[0], "0x%08X" % struct.unpack_from("<I", r, 24)[0],
            p10 * 10, "0x%02X" % cat, "0x%013X" % eq, fg])


def cmd_shops(g, a, out):
    w = csv.writer(out, lineterminator="\n")
    w.writerow(["table", "row", "count", "entries"])
    el = _rows64(g.read("menu/el_shop.bin"), 32)
    for i, r in enumerate(el):
        r = trim(r)
        if r:
            w.writerow(["el_shop", i, len(r), "; ".join("%d:%s" % (v, g.sysmes(0x17, v)) for v in r)])
    ws = _rows64(g.read("menu/waa_shop.bin"), 64)
    for i, r in enumerate(ws):
        r = trim(r)
        if r:
            info = load_item_info(g)
            w.writerow(["waa_shop", i, len(r), "; ".join("0x%X:%s@%d" % (v, g.sysmes(0, v), info[v & 0xFF][1] * 10) for v in r)])
    tr = _rows64(g.read("menu/trade.bin"), 32)
    for i, r in enumerate(tr):
        r = trim(r)
        if r:
            pairs = ["0x%04X x%d" % (r[k], r[k + 1]) for k in range(0, len(r) - 1, 2)]
            w.writerow(["trade", i, len(pairs), "; ".join(pairs)])


# --------------------------------------------------------------------------
# battle script (.pac)
# --------------------------------------------------------------------------

# Instruction length (opcode byte included) per opcode 0x00..0xE4, 0 = invalid/unused.
# Source: table at battle.prg file offset 0x351F0, indexed by the interpreter with the opcode.
# Verified: it tiles all 13,743 script bodies in
# battle/event/*.pac exactly and every goto/cond-jump target lands on an instruction start.
BS_LEN = bytes.fromhex(
    "0103080000030003030301010001030306030306060606030306060605050305"
    "060608080808080303030106010302050304040302030302010201010303030b"
    "0d030b0d06080802080304030c100505050505050105050303010404040b0602"
    "01070503040101080b0505050303050708030305070808010306010301020400"
    "030205030305030103070105030d030705030402050503010505050605020301"
    "02080405010301050404050504020502040402040503050406080a0407040000"
    "0505050505050000000000000000000000000000000000000000000000000000"
    "0407010608")

def bs_names():
    """Opcode names of the engine's own table (src/battle/pac_opcodes.inc), so the dump and the engine cannot drift."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "src", "battle", "pac_opcodes.inc")
    names = {}
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            m = re.match(r'\s*PAC_OP\(0x([0-9A-Fa-f]+),\s*\d+,\s*"([^"]*)"', line)
            if m and m.group(2):
                names[int(m.group(1), 16)] = m.group(2)
    return names


BS_NAME = bs_names()

BS_ENTITY_STRIDE = 0x40


def bs_parse(d):
    """Parse one .pac.  Returns dict(entity_count, base, end, scripts=[(entity,slot,start,end)], extra)."""
    c = u32(d, 0)
    offs = [u32(d, 4 + 4 * i) for i in range(c + 1)]
    ec = u32(d, 0x90)
    base = 0x94 + ec * BS_ENTITY_STRIDE      # script area origin (file offset); pointers are relative to it
    end = offs[1]                            # end of the script block (= start of the second part)
    ptrs = []
    extra = 0
    for e in range(ec):
        for i in range(32):
            p = u16(d, 0x94 + e * BS_ENTITY_STRIDE + 2 * i)
            if p:
                ptrs.append((e, i, p))
                if i >= 5:
                    extra += 1
    uniq = sorted(set(p for _, _, p in ptrs))
    ext = {}
    for k, p in enumerate(uniq):
        ext[p] = (uniq[k + 1] if k + 1 < len(uniq) else end - base)
    scripts = [(e, i, base + p, base + ext[p]) for e, i, p in ptrs]
    if uniq and uniq[0] > 0:
        # code at rel 0 up to the first pointer: entity 0 slot 0 (pointer value 0 = "init at area start")
        scripts.insert(0, (0, 0, base, base + uniq[0]))
    return {"count": c, "offs": offs, "entities": ec, "base": base, "end": end, "scripts": scripts, "extra": extra,
            "ptr_extra": extra}


def bs_decode(d, s, e):
    """Yield (offset, opcode, length) for script bytes [s,e).  length 0 / overrun -> raises ValueError."""
    p = s
    while p < e:
        op = d[p]
        n = BS_LEN[op] if op < len(BS_LEN) else 0
        if n == 0 or p + n > e:
            raise ValueError("bad op %02X at %X (len %d, left %d)" % (op, p, n, e - p))
        yield p, op, n
        p += n


def cmd_bscript(g, a, out):
    mode = a.rest[0] if a.rest else "stats"
    pacs = sorted(g.names("battle/event/"))
    if mode == "dump":
        name = a.rest[1]
        d = g.read("battle/event/" + name)
        info = bs_parse(d)
        out.write("%s: entities=%d base=0x%X end=0x%X\n" % (name, info["entities"], info["base"], info["end"]))
        seen = set()
        for e, i, s, en in info["scripts"]:
            if (s, en) in seen:
                continue
            seen.add((s, en))
            out.write("entity %d script %d  [%04X..%04X) rel %04X\n" % (e, i, s, en, s - info["base"]))
            for p, op, n in bs_decode(d, s, en):
                out.write("  %04X: %02X %-20s %s\n" % (p - info["base"], op, BS_NAME.get(op, ""), d[p + 1:p + n].hex(" ")))
        return
    hist = {}
    bad = []
    nfiles = nscripts = 0
    ent_hist = {}
    extra_files = []
    endops = {}
    uniq_total = 0
    for n in pacs:
        d = g.read(n)
        info = bs_parse(d)
        nfiles += 1
        ent_hist[info["entities"]] = ent_hist.get(info["entities"], 0) + 1
        if info["extra"]:
            extra_files.append(n)
        seen = set()
        for e, i, s, en in info["scripts"]:
            if (s, en) in seen:
                continue
            seen.add((s, en))
            nscripts += 1
            last = None
            try:
                for p, op, ln in bs_decode(d, s, en):
                    hist[op] = hist.get(op, 0) + 1
                    last = op
                endops[last] = endops.get(last, 0) + 1
            except ValueError as ex:
                bad.append((n, e, i, str(ex)))
    out.write("# bscript stats: %d files, %d unique scripts, %d undecodable\n" % (nfiles, nscripts, len(bad)))
    out.write("# entity counts (file count): %s\n" % ", ".join("%d:%d" % kv for kv in sorted(ent_hist.items())))
    out.write("# files with pointers in slots >=5 of an entity record: %d %s\n" % (len(extra_files), extra_files[:6]))
    out.write("# last opcode of each script: %s\n" % ", ".join("%02X:%d" % kv for kv in sorted(endops.items())))
    out.write("opcode,length,name,count\n")
    for op in range(len(BS_LEN)):
        if BS_LEN[op] or op in hist:
            out.write("0x%02X,%d,%s,%d\n" % (op, BS_LEN[op], BS_NAME.get(op, ""), hist.get(op, 0)))
    out.write("# invalid opcodes (length 0) seen in corpus: %s\n" % [hex(o) for o in hist if not BS_LEN[o]])
    out.write("# undecodable scripts:\n")
    for b in bad:
        out.write("#   %s entity %d slot %d: %s\n" % b)


# --------------------------------------------------------------------------
# identify
# --------------------------------------------------------------------------

# name -> (structure, record stride, note)
KNOWN = {
    "battle/data/monster.bin": ("enemy stat records", 0xB0, "222 enemies; = player.dat part 1; index = enemy id (sysmes 0x19)"),
    "battle/data/char.bin": ("player stat records", 0xB0, "51 chars (id order of save template) -- no trailer"),
    "battle/data/char20.bin": ("player stat records, dream/alt set", 0xB0, "51 chars; same layout"),
    "battle/data/char_w0.bin": ("player stat records, preset 0", 0xB0, "51 chars; no lv99 trailer"),
    "battle/data/char_w1.bin": ("player stat records, preset 1 + lv99para trailer", 0xB0, "51x0xB0 + 51x8"),
    "battle/data/char_w2.bin": ("player stat records, preset 2 + lv99para trailer", 0xB0, "51x0xB0 + 51x8"),
    "battle/data/char_w3.bin": ("player stat records, preset 3 + lv99para trailer", 0xB0, "51x0xB0 + 51x8"),
    "battle/data/char_w4.bin": ("player stat records, preset 4 + lv99para trailer", 0xB0, "51x0xB0 + 51x8"),
    "battle/data/char_w5.bin": ("player stat records, preset 5 + lv99para trailer", 0xB0, "51x0xB0 + 51x8"),
    "battle/data/lv99para.bin": ("level-99 target stats", 8, "51 x {u16 hp,atk,def,hit,evd,mag,mdef}; = exdat part 6"),
    "battle/data/el_mask.bin": ("element grid growth table", 8, "100 star levels x 8 growth columns; = exdat part 1"),
    "battle/data/pgdat.bin": ("element/tech battle records", 0x2C, "527 records; id = sysmes 0x17 index; = eqp.dat decoded"),
    "battle/data/eqp.dat": ("cpt[1] -> sszl(pgdat.bin)", 0, "misnamed: compressed copy of pgdat.bin"),
    "battle/data/menu_pg.bin": ("menu-side element table", 16, "287 x 16B (regular only in the first rows); = exdat part 4"),
    "battle/data/menu_waa.bin": ("menu-side per-level table", 0, "3072 B; first ~10 rows look like 12B records {.., u16 value, u16 step}; = exdat part 5"),
    "battle/data/waa.bin": ("battle-side waza table", 0, "7168 B; = player.dat part 4; stride unresolved"),
    "battle/data/exdat.dat": ("cpt[7] of sszl blobs", 0, "parts: 0 aux(400B,100x4), 1 el_mask, 2 ditem, 3 aux(652B), 4 menu_pg, 5 menu_waa, 6 lv99para"),
    "battle/data/player.dat": ("cpt[6] of sszl blobs", 0, "parts: 0 scenedat, 1 monster, 2 symtbl, 3 btbgdat, 4 waa, 5 btmes"),
    "battle/data/ditem.dat": ("sszl(drop table)", 8, "256 rows x 4 u16 (bit15 = element id, else item id); = exdat part 2"),
    "battle/data/scenedat.bin": ("encounter scene (group) records", 28, "513 scenes; scene i <-> evt%03d.pac; = player.dat part 0"),
    "battle/data/symtbl.bin": ("encounter id x 8 colour variants -> scene", 16, "384 x 8 u16; = player.dat part 2"),
    "battle/data/btbgdat.bin": ("battle background -> 3 initial field colours", 3, "100 x 3; = player.dat part 3"),
    "battle/data/btmes.dat": ("battle message table", 8, "u32 header? + 8-byte records; = player.dat part 5"),
    "battle/data/testview.dat": ("debug viewer params", 0, "20 bytes"),
    "menu/el_shop.bin": ("shop element stock", 64, "32 shops x 32 u16 element ids, 0-terminated"),
    "menu/waa_shop.bin": ("shop equipment stock", 64, "64 rows x 32 u16 item ids, 0-terminated"),
    "menu/trade.bin": ("trade/forge lists", 64, "32 rows x 16 (u16 typed-id, u16 qty) pairs"),
    "menu/waa_tbl.bin": ("menu waza table (mostly empty)", 0, "8192 B; stride unresolved"),
    "menu/waa_name.dat": ("u16 offset table + JP strings", 0, "256 names (first offset 0x202)"),
    "menu/waa_spec.dat": ("u16 offset table + JP strings", 0, "768 descriptions (first offset 0x602) = waa_spec.txt rows"),
    "menu/keyitem.dat": ("u16 offset table + JP strings", 0, "195 descriptions (first offset 0x188) = keyitem.txt rows (3 per key item)"),
    "battle/help.prd": ("battle help overlay data", 0, "container, not parsed here"),
}


def guess_stride(d):
    """crude record-stride guess: stride whose same-column byte equality is highest."""
    if len(d) < 64:
        return 0
    best, bs = 0.0, 0
    n = min(len(d), 8192)
    for s in range(2, 97):
        eq = sum(1 for i in range(n - s) if d[i] == d[i + s])
        sc = eq / (n - s)
        if sc > best + 0.02:
            best, bs = sc, s
    return bs


def cmd_identify(g, a, out):
    names = sorted(n for n in g.names("battle/data/")) + sorted(n for n in g.names("menu/") if n.count("/") == 1)
    w = csv.writer(out, lineterminator="\n")
    w.writerow(["file", "size", "container", "decoded_size", "structure", "stride", "records", "note"])
    for n in names:
        d = g.read(n)
        kind = "raw"
        dec = len(d)
        body = d
        if d[:4] == b"sszl":
            kind = "sszl"
            body = lzss_decode(d)
            dec = len(body)
        elif n.endswith(".tim") or d[:4] == b"\x10\x00\x00\x00":
            kind = "tim"
        else:
            sub = cpt_split(d) if n not in ("battle/data/btbgdat.bin",) else None
            if sub and len(sub) <= 64:
                kind = "cpt[%d]" % len(sub)
        k = KNOWN.get(n)
        if not k and n.endswith(".prd"):
            k = (".prd resource pack / overlay data", 0, "not parsed here")
        if not k and n.endswith(".ome"):
            k = ("character profile data", 0, "not parsed here")
        if k:
            st, stride, note = k
        else:
            st, stride, note = "unknown", guess_stride(body) if kind in ("raw", "sszl") else 0, ""
        recs = (len(body) // stride) if stride and kind != "cpt" else ""
        if n.startswith("battle/data/char_w") and len(body) > 51 * 176:
            recs = "51 (+51x8 lv99 trailer)"
        if stride and kind.startswith("cpt"):
            recs = ""
        w.writerow([n, len(d), kind, dec, st, stride, recs, note])


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------

COMMANDS = {
    "identify": cmd_identify, "enemies": cmd_enemies, "elements": cmd_elements, "groups": cmd_groups,
    "drops": cmd_drops, "growth": cmd_growth, "chars": cmd_chars, "keyitems": cmd_keyitems,
    "shops": cmd_shops, "gear": cmd_gear, "bscript": cmd_bscript,
}


def main(argv=None):
    import argparse
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=sorted(COMMANDS))
    ap.add_argument("rest", nargs="*", help="bscript: stats | dump <evtNNN.pac>")
    ap.add_argument("--game", default=None, help="RDE install dir (default: parent of OpenCross)")
    ap.add_argument("-o", "--output", default=None)
    ap.add_argument("--file", default="char.bin", help="chars: char.bin|char20.bin|char_w0..5.bin")
    ap.add_argument("--symtbl", action="store_true", help="groups: also print encounter->scene map")
    a = ap.parse_args(argv)
    g = Game(a.game)
    if a.output:
        with open(a.output, "w", encoding="utf-8", newline="") as f:
            COMMANDS[a.command](g, a, f)
    else:
        COMMANDS[a.command](g, a, sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
