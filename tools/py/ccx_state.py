"""ccx (DuckStation) save-state reader: PS1 RAM and VRAM out of a .sav, plus a battle-state watch table.

    python tools/py/ccx_state.py info  <state.sav>                 offsets + the watch row of one state
    python tools/py/ccx_state.py watch <dir-or-state> [...]        one row per state (sorted), the named addresses below
    python tools/py/ccx_state.py extract <state.sav> <out-prefix>  writes <prefix>.ram (8 MiB) and <prefix>.vram (1 MiB, 2048 B/row)
    python tools/py/ccx_state.py hex <state.sav> <addr> [len]      hex dump of guest RAM
    python tools/py/ccx_state.py poke <in.sav> <out.sav> ADDR=VAL[:WIDTH] ...   patch guest RAM in an uncompressed state

Layout: header + two zstd frames; the second decompresses to the ~10 MB body. The body holds the 8 MiB
guest RAM (ccx runs with Enable8MBRAM) and the 1 MiB VRAM. Both are found by signature, not by fixed offset: the main
exe's first words at guest 0x80010000 (`0800e003 00000000 0780023c 803b4224`) locate RAM, the "GPU-VRAM" tag
precedes VRAM. Verified on `rde-reimpl\\bin\\m0-battle.sav` and other state bodies.
Needs the `zstandard` module (present in Python 3.13).
"""
import os
import struct
import sys

import zstandard

ZSTD_MAGIC = b"\x28\xb5\x2f\xfd"
EXE_SIG = bytes.fromhex("0800e003000000000780023c803b4224")  # guest 0x80010000
EXE_ADDR = 0x10000
RAM_SIZE = 0x800000
VRAM_TAG = b"GPU-VRAM"
VRAM_SIZE = 0x100000

# Watch table: (name, guest address, width in bytes, signed). Notes: unit U(i) =
# 0x800E06A0 + 0x410 i, current action e0666/e0667/e0668, effect load state 0x800720F8, overlay resident 0x800856F0,
# battle id 0x80075906, area 0x800790FC, location u16 0x80077C26, field ring 0x800DFF98 (slots at bytes 0/4/8).
WATCH = [
    ("ovl_id", 0x800A9F00, 4, False),
    ("mt_ovl", 0x801D2800, 4, False),
    ("mt_mode", 0x801F5488, 1, False),
    ("mt_cur", 0x801F54A8, 1, False),
    ("mt_scroll", 0x801F54A0, 1, False),
    ("title_var", 0x801F377C, 1, False),
    ("mt_5480", 0x801F5480, 1, False),
    ("mt_5478", 0x801F5478, 1, False),
    ("mt_547c", 0x801F547C, 1, False),
    ("ld_5010", 0x801F5010, 1, False),
    ("ld_cur", 0x801F4FE4, 4, False),
    ("ld_card", 0x801F502C, 4, False),
    ("pad_e0", 0x800855E0, 2, False),
    ("pad_e2", 0x800855E2, 2, False),
    ("pad_e4", 0x800855E4, 2, False),
    ("pad_e6", 0x800855E6, 2, False),
    ("loc", 0x80077C26, 2, False),
    ("btl_id", 0x80075906, 2, False),
    ("area", 0x800790FC, 2, False),
    ("btl_frame", 0x800E0518, 4, False),
    ("tgt_064b", 0x800E064B, 1, False),
    ("phase_fc40", 0x800DFC40, 1, False),
    ("pc_0649", 0x800E0649, 1, False),
    ("menu_064a", 0x800E064A, 1, False),
    ("cur_064e", 0x800E064E, 1, False),
    ("e065a", 0x800E065A, 1, False),
    ("e0652", 0x800E0652, 1, False),
    ("tgt_0650", 0x800E0650, 1, False),
    ("ui_0642", 0x800E0642, 1, False),
    ("hint_f0ae", 0x800DF0AE, 1, False),
    ("act_kind", 0x800E0666, 1, False),
    ("act_str", 0x800E0667, 1, False),
    ("act_elem", 0x800E0668, 2, False),
    ("act_actor", 0x800E0634, 1, False),
    ("e05da", 0x800E05DA, 2, False),
    ("tgt0", 0x800E0637, 1, False),   # current action target list (0xFF end)
    ("tgt1", 0x800E0638, 1, False),
    ("tgt2", 0x800E0639, 1, False),
    ("att_310e", 0x800E310E, 1, False),
    ("cur_310f", 0x800E310F, 1, False),
    ("u3_sta", 0x800E06A0 + 0x410 * 3 + 0x19E, 2, True),
    ("e3372", 0x800E3372, 2, False),
    ("fx_load", 0x800720F8, 4, False),
    ("ovl", 0x800856F0, 4, False),
    ("ring0", 0x800DFF98, 1, False),
    ("ring1", 0x800DFF9C, 1, False),
    ("ring2", 0x800DFFA0, 1, False),
    ("party0", 0x800786F4, 1, False),
    ("party1", 0x800786F5, 1, False),
    ("party2", 0x800786F6, 1, False),
    ("u0_hp", 0x800E06A0 + 0xB8, 2, True),
    ("u0_gauge", 0x800E06A0 + 0x16E, 2, True),
    ("u0_sta", 0x800E06A0 + 0x19E, 2, True),
    ("u3_hp", 0x800E06A0 + 0x410 * 3 + 0xB8, 2, True),
    ("u4_hp", 0x800E06A0 + 0x410 * 4 + 0xB8, 2, True),
    ("u5_hp", 0x800E06A0 + 0x410 * 5 + 0xB8, 2, True),
]


class State:
    def __init__(self, path):
        self.path = path
        raw = open(path, "rb").read()
        if raw[:5] != b"DUCCG":
            raise ValueError("%s: not a DuckStation state" % path)
        p = raw.find(ZSTD_MAGIC)
        frames = []
        while p != -1 and len(frames) < 4:
            frames.append(p)
            p = raw.find(ZSTD_MAGIC, p + 1)
        self.compressed = len(frames) >= 2
        if self.compressed:
            body = zstandard.ZstdDecompressor().decompressobj().decompress(raw[frames[1]:])
            self.body_file_off = None
        else:
            body = raw  # Main/SaveStateCompression=Uncompressed: the body sits in the file as is (pokeable in place)
            self.body_file_off = 0
        sig = body.find(EXE_SIG)
        if sig < EXE_ADDR:
            raise ValueError("%s: main exe signature not found" % path)
        self.ram_off = sig - EXE_ADDR
        tag = body.find(VRAM_TAG)
        if tag < 0:
            raise ValueError("%s: GPU-VRAM tag not found" % path)
        self.vram_off = tag + len(VRAM_TAG)
        self.ram = body[self.ram_off:self.ram_off + RAM_SIZE]
        self.vram = body[self.vram_off:self.vram_off + VRAM_SIZE]
        self.body_len = len(body)

    def _o(self, addr):
        return (addr & 0x1FFFFFFF) if addr >= 0x80000000 else addr

    def u8(self, a):
        return self.ram[self._o(a)]

    def u16(self, a):
        return struct.unpack_from("<H", self.ram, self._o(a))[0]

    def s16(self, a):
        return struct.unpack_from("<h", self.ram, self._o(a))[0]

    def u32(self, a):
        return struct.unpack_from("<I", self.ram, self._o(a))[0]

    def bytes_at(self, a, n):
        o = self._o(a)
        return self.ram[o:o + n]

    def watch(self, table=WATCH):
        row = {}
        for name, addr, width, signed in table:
            o = self._o(addr)
            v = int.from_bytes(self.ram[o:o + width], "little", signed=signed)
            row[name] = v
        # Derived: the element of the current action = (pgdat record pointer e30a0 - table base [e3368]) / 0x2C,
        # the field leader's position (field.prg: entity pointer 0x800E9F7C, +0x3C X / +0x44 Z, 16.16),
        # and the first word of the resident effect overlay at 0x801C5E00.
        p, base = self.u32(0x800E30A0), self.u32(0x800E3368)
        row["elem"] = (p - base) // 0x2C if base and p >= base and p - base < 0x2C * 600 else -1
        # Field entities: table [0x800E8894], stride 0x278, the controlled entity's index is the
        # byte 0x800E9F7C (FE 4C); X / Z are 16.16 at +0x3C / +0x44.
        ents, lead = self.u32(0x800E8894), self.u8(0x800E9F7C)
        if 0x80000000 <= ents < 0x80800000 and lead < 64:
            e = self._o(ents) + lead * 0x278
            row["lead_x"] = struct.unpack_from("<i", self.ram, e + 0x3C)[0] >> 16
            row["lead_z"] = struct.unpack_from("<i", self.ram, e + 0x44)[0] >> 16
        else:
            row["lead_x"] = row["lead_z"] = None
        row["ovl0"] = "%08x" % self.u32(0x801C5E00)
        return row

    def vram_pixel(self, x, y):
        """BGR555 halfword at VRAM (x, y); rows are 2048 bytes."""
        return struct.unpack_from("<H", self.vram, y * 2048 + x * 2)[0]


def frame_of(path):
    base = os.path.basename(path)
    digits = "".join(ch for ch in base if ch.isdigit())
    return int(digits) if digits else -1


def list_states(args):
    out = []
    for a in args:
        if os.path.isdir(a):
            out += [os.path.join(a, f) for f in os.listdir(a) if f.endswith(".sav")]
        else:
            out.append(a)
    return sorted(out, key=frame_of)


def window(paths, elem=None):
    """(first, last) state frames of an element cast: first = act_kind 1 and fx_load != 0 (the pack load starts, e.g. run frame 30 of a
    PC cast), last = the last state with act_kind 1 and the effect overlay resident (ovl != 0; it comes ~100 frames after the load and
    ends with the pack). elem filters on e05da."""
    first = last = ovl_first = ovl_last = None
    for p in paths:
        r = State(p).watch()
        if r["act_kind"] != 1 or (elem is not None and r["e05da"] != elem):
            continue
        f = frame_of(p)
        if first is None and r["fx_load"] != 0:
            first = f
        if first is None and ovl_first is None and r["ovl"] != 0:
            ovl_first = f   # scripted casts: the fx_load blip can fall between two states; the resident overlay still marks the pack
        if first is not None and (r["ovl"] != 0 or r["fx_load"] != 0):
            last = f
        if r["ovl"] != 0:
            ovl_last = f
    if first is None and ovl_first is not None:
        return (ovl_first, ovl_last)
    return (first, last if last is not None else first)


def manifest(paths, elem=None):
    first, last = window(paths, elem)
    out = ["cast_frame: %s (first state with act_kind 1 and fx_load != 0)" % first, "effect_last_frame: %s" % last]
    for p in paths:
        if frame_of(p) == first:
            st = State(p)
            r = st.watch()
            out.append("caster_unit: %d  target_unit: %d  (act_actor / e0637)  party0: %d" % (r["act_actor"], r["tgt0"], r["party0"]))
            ov = next((p2 for p2 in paths if frame_of(p2) >= first and State(p2).watch()["ovl"] != 0), None)
            out.append("pack_resident_from: %s (first state with ovl != 0)" % (frame_of(ov) if ov else None))
            for nm, stt in (("at cast", st), ("at pack start", State(ov) if ov else st)):
                eye = [stt.s16(0x80071F68 + 2 * k) for k in range(3)]
                tg = [stt.s16(0x80071F70 + 2 * k) for k in range(3)]
                out.append("camera %s: eye %s target %s H %d (RAM 0x80071F68 / 70 / 84; pick name: ref_info.py)" % (nm, eye, tg, stt.u16(0x80071F84)))
            break
    return chr(10).join(out)


def harvest(paths, out):
    """Runs of consecutive states with the same (actor, element) while an element effect is active (act_kind 1 and the pack loading
    or resident: fx_load != 0 or ovl != 0). The target is the first target unit (e0637) at the run's first state."""
    runs, cur = [], None
    for p in paths:
        r = State(p).watch()
        act = r["act_kind"] == 1 and (r["fx_load"] != 0 or r["ovl"] != 0)
        key = (r["act_actor"], r["e05da"])
        f = frame_of(p)
        if act and cur and cur[0] == key and f - cur[2] <= 180:   # the load blip (fx_load) and the resident overlay are ~100 frames apart
            cur[2] = f
        elif act:
            if cur:
                runs.append(cur)
            cur = [key, f, f, r["tgt0"]]
    if cur:
        runs.append(cur)
    with open(out, "w") as fh:
        fh.write("\t".join(["start", "end", "unit", "element", "target"]) + "\n")
        for (a, e), f0, f1, t in runs:
            fh.write("\t".join(str(v) for v in (f0, f1, a, e, t)) + "\n")
    print("harvested %d runs -> %s" % (len(runs), out))


def campick(paths):
    """Sequence-task tags / names that are camera shots, in order of first appearance (fx_ref.py seqs tags)."""
    import re
    import fx_ref as fr
    shot = re.compile(r"^(ch\d+|tgt\w*|apr\w*|\d+\w?)$")
    seen = []
    for p in paths:
        try:
            st = State(p)
            reg = fr.Registry(st)
            base, ts = fr.tasks(st)
            for e, cb, d in ts:
                if cb == fr.CB_SEQ:
                    for n in (fr.tag(st.u32(d + 0x10)), reg.name_of_data(st.u32(d + 4))):
                        n = n.rstrip(".")
                        if shot.match(n) and n.strip("0") and n not in seen:
                            seen.append(n)
        except Exception:
            continue
    return ",".join(seen)


def fmt_row(row):
    return " ".join("%s=%s" % (k, v) for k, v in row.items())


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    cmd = argv[0]
    if cmd == "info":
        st = State(argv[1])
        print("%s: body %d B, RAM at %d, VRAM at %d" % (argv[1], st.body_len, st.ram_off, st.vram_off))
        print(fmt_row(st.watch()))
    elif cmd == "watch":
        fields = None
        rest = argv[1:]
        if rest and rest[0] == "--fields":
            fields = rest[1].split(",")
            rest = rest[2:]
        for p in list_states(rest):
            try:
                st = State(p)
            except Exception as e:  # noqa: BLE001
                print("%s: %s" % (p, e))
                continue
            row = st.watch()
            if fields:
                row = {k: row.get(k) for k in fields}
            print("%6d %s" % (frame_of(p), fmt_row(row)))
    elif cmd == "extract":
        st = State(argv[1])
        open(argv[2] + ".ram", "wb").write(st.ram)
        open(argv[2] + ".vram", "wb").write(st.vram)
        print("wrote %s.ram (%d) and %s.vram (%d)" % (argv[2], len(st.ram), argv[2], len(st.vram)))
    elif cmd == "poke":
        # poke <in.sav> <out.sav> ADDR=VALUE[:WIDTH] ... (uncompressed states only: ccx -set Main/SaveStateCompression=Uncompressed)
        st = State(argv[1])
        if st.compressed:
            raise SystemExit("poke needs an uncompressed state (save it with -set Main/SaveStateCompression=Uncompressed)")
        data = bytearray(open(argv[1], "rb").read())
        for spec in argv[3:]:
            a, v = spec.split("=")
            width = 1
            if ":" in v:
                v, width = v.split(":")
                width = int(width)
            addr, val = int(a, 0), int(v, 0)
            off = st.ram_off + st._o(addr)
            old = int.from_bytes(data[off:off + width], "little")
            data[off:off + width] = val.to_bytes(width, "little")
            print("poke %08X: %d -> %d (%d bytes)" % (addr, old, val, width))
        open(argv[2], "wb").write(data)
        print("wrote", argv[2])
    elif cmd == "window":
        # window <dir> [elem]: first / last state frame whose action is an element cast (act_kind 1, fx_load != 0, e05da == elem)
        print(window(list_states([argv[1]]), int(argv[2]) if len(argv) > 2 else None))
    elif cmd == "manifest":
        # manifest <dir> [elem]: cast frame (first state of the effect), caster / target unit, raw camera, the watch table
        print(manifest(list_states([argv[1]]), int(argv[2]) if len(argv) > 2 else None))
    elif cmd == "prune":
        # prune <dir> <first> <last>: delete states (and ../frames PNGs) whose run frame is outside [first, last]
        lo, hi = int(argv[2]), int(argv[3])
        n = 0
        for d in (argv[1], os.path.join(os.path.dirname(argv[1].rstrip("/\\")), "frames")):
            if os.path.isdir(d):
                for f in os.listdir(d):
                    if frame_of(f) not in range(lo, hi + 1) and f.endswith((".sav", ".png")):
                        os.remove(os.path.join(d, f))
                        n += 1
        print("pruned", n)
    elif cmd == "campick":
        # campick <dir>: camera shot sequences (ch10..ch13, tgtN, aprN, numbered shots) by first appearance over the states, comma list
        print(campick(list_states([argv[1]])))
    elif cmd == "harvest":
        # harvest <dir> <out.tsv>: enemy / PC element casts by run of (actor, element) over the sampled states
        harvest(list_states([argv[1]]), argv[2])
    elif cmd == "hex":
        st = State(argv[1])
        addr = int(argv[2], 0)
        n = int(argv[3], 0) if len(argv) > 3 else 64
        b = st.bytes_at(addr, n)
        for i in range(0, len(b), 16):
            print("%08X  %s" % (addr + i, b[i:i + 16].hex(" ")))
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
