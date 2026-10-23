"""Chrono Cross room .prd decoder: effect emitters (type 0x0A), timinfo (0x03), sequences (0x10).

Usage (always run with -I):
  python -I cc_prd.py --game <RDE dir> info <room>            decoded records per entry
  python -I cc_prd.py --game <RDE dir> validate               parse all room .prd files + statistics
  python -I cc_prd.py --game <RDE dir> render <room> <outdir> every timinfo sprite slice (and every frame) as PNG

Stdlib only; reads the game archive read-only.
"""
import argparse
import collections
import os
import struct
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cc_container as cont
import cc_lzss
import cc_tim

MAPDIR = "map/mapbin/"
PARTICLE = "effect/particle.prd"      # shared particle library, loaded in every field [I: load site not traced]

# ------------------------------------------------------------------ 0x0A effect stream
# op -> (name, [lengths], [(label, offset, fmt)])   fmt: h s16, H u16, B u8, I u32, n 4-char name, o oscillator(3 x s16)
OSC3 = [("x", 2, "o"), ("y", 10, "o"), ("z", 18, "o")]
XYZ_S = [("x", 2, "h"), ("y", 4, "h"), ("z", 6, "h")]
XYZ_U = [("x", 2, "H"), ("y", 4, "H"), ("z", 6, "H")]
EFFECT_OPS = {
    0x01: ("BEGIN", [2], []),
    0x03: ("OT_BIAS", [4], [("bias", 2, "h")]),
    0x04: ("ANIM_PERIOD", [4], [("ticks", 2, "B")]),
    0x05: ("SPRITE", [16], [("frame0", 2, "H"), ("cache", 4, "I"), ("name", 8, "n"), ("type", 12, "h")]),
    0x06: ("FLAGS", [8], [("flags", 4, "I")]),
    0x08: ("POS", [8], XYZ_S),
    0x09: ("POS_ADD_OSC", [26], OSC3),
    0x0A: ("VEL", [8], XYZ_S),
    0x0B: ("VEL_ADD_RAND", [8], XYZ_S),
    0x0C: ("ACC", [8], XYZ_S),
    0x0D: ("ACC_ADD_OSC", [26], OSC3),
    0x0E: ("ACC_ADD_RAND", [8], XYZ_S),
    0x0F: ("LIFE", [4], [("ticks", 2, "H")]),
    0x10: ("LIFE_ADD", [4], [("ticks", 2, "H")]),
    0x11: ("LIFE_ADD_OSC", [10], [("osc", 2, "o")]),
    0x12: ("SCALE", [8], XYZ_U),
    0x13: ("SCALE_ADD_RAND", [8], XYZ_U),
    0x14: ("SCALE_ADD_OSC", [26], OSC3),
    0x15: ("SCALE_RATE", [8], XYZ_U),
    0x16: ("SCALE_RATE_ADD_OSC", [26], OSC3),
    0x17: ("NOP17", [8], XYZ_S),
    0x18: ("SCALE_RATE_ADD_RAND", [8], XYZ_S),
    0x19: ("ANGLE", [8], XYZ_U),
    0x1A: ("ANGLE_ADD_RAND", [8], XYZ_S),
    0x1B: ("ANGLE_RATE", [8], XYZ_U),
    0x1C: ("ANGLE_ADD_OSC", [26], OSC3),
    0x1D: ("NOP1D", [8], XYZ_S),
    0x1E: ("ANGLE_RATE_ADD_RAND", [8], XYZ_S),
    0x1F: ("ANGLE_RATE_ADD_OSC", [26], OSC3),
    0x20: ("COLOR", [8], [("r", 2, "H"), ("g", 4, "H"), ("b", 6, "H")]),
    0x21: ("COLOR_ADD_OSC", [26], OSC3),
    0x22: ("COLOR_RATE", [8], [("r", 2, "H"), ("g", 4, "H"), ("b", 6, "H")]),
    0x23: ("COLOR_RATE_ADD_OSC", [26], OSC3),
    0x24: ("NOP24", [8], XYZ_S),
    0x25: ("COLOR_ADD_RAND", [8], XYZ_S),
    0x26: ("COLOR_RATE_ADD_RAND", [8], XYZ_S),
    0x27: ("POS_ADD_BLOB", [22, 26], [("span", 2, "h"), ("base", 4, "h"), ("sx", 6, "h"), ("sy", 8, "h"),
                                      ("sz", 10, "h"), ("base_grow", 12, "h"), ("span_grow", 14, "h")]),
    0x2F: ("VEL_ADD_OSC", [26], OSC3),
    0x30: ("ANCHOR", [12], [("ptr", 4, "I")]),
    0x31: ("BOUNCE", [4], [("restitution", 2, "B")]),
    0x32: ("FLOOR_Y", [4], [("y", 2, "H")]),
    0x33: ("ON_DEATH_SPAWN", [12], [("cache", 4, "I"), ("name", 8, "n")]),
    0x34: ("VEL_ADD_AWAY", [4], [("k", 2, "H")]),
    0x35: ("EXT35", [8], [("a", 4, "H"), ("b", 6, "H")]),
    0x36: ("EXT36", [14], [("p", 2, "I"), ("q", 6, "I"), ("r", 10, "h"), ("s", 12, "h")]),
    0x37: ("EXT37", [16], [("mode", 2, "H"), ("arg", 4, "H"), ("cache", 8, "I"), ("name", 12, "n")]),
    0x38: ("INTEGRATE", [4], []),
    0x39: ("ORIENT_BASIS", [10], [("a", 2, "B"), ("b", 3, "B"), ("c", 4, "B"), ("d", 5, "B"), ("f", 6, "H")]),
    0x3A: ("DRAG", [6], [("base", 2, "H"), ("rand", 4, "H")]),
    0x3B: ("FACE_PARENT_YAW", [4], []),
    0x3C: ("PERIODIC_SPAWN", [12], [("cache", 4, "I"), ("name", 8, "n")]),
    0x3D: ("PATH_POS", [20], [("curve", 2, "H"), ("cache", 4, "I"), ("name", 8, "n")]),
    0x3E: ("EXT3E", [24], [("a", 2, "H"), ("cache", 4, "I"), ("name", 8, "n")]),
    0x3F: ("EXT3F", [4], []),
    0x40: ("EXT40", [4], []),
    0x41: ("VEL_RANDOM_DIR", [14], [("ax", 2, "H"), ("ay", 4, "H"), ("az", 6, "H"), ("rx", 8, "h"), ("ry", 10, "h"),
                                    ("rz", 12, "h")]),
    0x42: ("AIM_AT_BASIS", [4], []),
    0x43: ("SCALE_ACC", [8], XYZ_U),
    0x44: ("ANGLE_ACC", [8], XYZ_U),
    0x45: ("SPAWN_WITH", [12], [("cache", 4, "I"), ("name", 8, "n")]),
    0x46: ("FACE_PARENT", [4], []),
    0x48: ("OP48", [12], [("ptr", 4, "I")]),
    0x49: ("OP49", [10], [("a", 2, "B"), ("b", 3, "B"), ("c", 4, "B"), ("clone", 5, "B"), ("d", 6, "H"), ("n", 8, "h")]),
}
# ops that the spawner ignores (no case label)
NOP_OPS = {0x01, 0x17, 0x1D, 0x24}


class StreamError(Exception):
    pass


def _fld(b, base, off, fmt):
    o = base + off
    if fmt == "h":
        return struct.unpack_from("<h", b, o)[0]
    if fmt == "H":
        return struct.unpack_from("<H", b, o)[0]
    if fmt == "B":
        return b[o]
    if fmt == "I":
        return struct.unpack_from("<I", b, o)[0]
    if fmt == "n":
        return b[o:o + 4].split(b"\0")[0].decode("latin1")
    if fmt == "o":
        return struct.unpack_from("<3h", b, o)          # amplitude, frequency, phase
    raise ValueError(fmt)


class Cmd:
    __slots__ = ("off", "op", "length", "fields")

    def __init__(self, off, op, length, fields):
        self.off, self.op, self.length, self.fields = off, op, length, fields

    def __str__(self):
        nm = EFFECT_OPS.get(self.op, ("OP%02X" % self.op,))[0]
        return "%04X %-20s %s" % (self.off, nm, " ".join("%s=%s" % (k, v) for k, v in self.fields))


def parse_effect(b):
    """Decode a type-0x0A payload.  Returns (header dict, [Cmd]).  Raises StreamError."""
    if len(b) < 0x1A:
        raise StreamError("short 0x0A payload")
    hd = dict(start=struct.unpack_from("<h", b, 4)[0], duration=struct.unpack_from("<h", b, 6)[0],
              period=struct.unpack_from("<H", b, 0xA)[0], repeat=struct.unpack_from("<h", b, 0xE)[0],
              flags=struct.unpack_from("<I", b, 0x10)[0])
    cmds = []
    p = 0x18
    while True:
        if p + 2 > len(b):
            raise StreamError("stream runs off the payload at 0x%X" % p)
        w = struct.unpack_from("<H", b, p)[0]
        op, ln = w & 0x1FF, (w >> 9) & 0x3F
        if w & 0x8000:
            raise StreamError("jump-flagged command at 0x%X" % p)
        if op == 0:
            break
        spec = EFFECT_OPS.get(op)
        if spec is None:
            raise StreamError("unknown op 0x%02X at 0x%X" % (op, p))
        if ln not in spec[1]:
            raise StreamError("op 0x%02X length %d at 0x%X" % (op, ln, p))
        if p + ln > len(b):
            raise StreamError("command overruns payload at 0x%X" % p)
        fields = [(lab, _fld(b, p, off, fm)) for lab, off, fm in spec[2]]
        cmds.append(Cmd(p, op, ln, fields))
        p += ln
    return hd, cmds


def flags_text(f):
    mode = f & 7
    names = {4: "floor-bounce", 8: "inherit-angles", 9: "inherit-vel", 10: "inherit-scale", 11: "inherit-color",
             12: "lod-frame", 13: "face-vel-3d", 14: "face-vel-yaw", 15: "parent-space", 16: "zyx",
             17: "depth-cue", 23: "fixed-slot", 24: "z-push", 25: "no-camera", 26: "premul-camera",
             27: "no-death-spawn", 28: "cull-test", 29: "accum"}
    return "blend=%d %s" % (mode, ",".join(n for b, n in sorted(names.items()) if f >> b & 1))


# ------------------------------------------------------------------ 0x10 sequence stream
SEQ_OPS = {0x20: ("MARK", 8), 0x21: ("END", 8), 0x22: ("START_EMITTER", 16), 0x23: ("SOUND_GROUP", 16),
           0x28: ("HOLD", 8), 0x29: ("RAMP", 12), 0x2E: ("OP2E", 12), 0x2F: ("CHILD_SEQ", 16),
           0x3E: ("BRACKET_BEGIN", 8), 0x3F: ("BRACKET_END", 8), 0x65: ("OP65", 16)}


def parse_sequence(b):
    """Type-0x10 payload: 0x14-byte header then u16-headed commands ending at op 0x21.
    Returns [(off, op, wait_after, extra)]; extra = name for ops with a name at +12."""
    out = []
    p = 0x14
    while True:
        if p + 4 > len(b):
            raise StreamError("sequence runs off the payload")
        w = struct.unpack_from("<H", b, p)[0]
        op, ln = w & 0x1FF, (w >> 9) & 0x3F
        spec = SEQ_OPS.get(op)
        if spec is None or spec[1] != ln:
            raise StreamError("sequence op 0x%02X length %d at 0x%X" % (op, ln, p))
        wait = struct.unpack_from("<H", b, p + 2)[0]
        name = b[p + 12:p + 16].split(b"\0")[0].decode("latin1") if ln >= 16 else None
        out.append((p, op, wait, name))
        p += ln
        if op == 0x21:
            break
    return out


# ------------------------------------------------------------------ 0x03 timinfo
class Sprite:
    __slots__ = ("x", "y", "u", "v", "clut", "w", "h", "flags")

    @property
    def page_x(self):
        return (self.flags & 0xF) * 64          # halfwords

    @property
    def page_y(self):
        return 256 if self.flags & 0x10 else 0

    @property
    def abr(self):
        return (self.flags >> 5) & 3

    @property
    def depth(self):                            # 0 = 4bpp, 1 = 8bpp, 2 = 16bpp
        return (self.flags >> 7) & 3

    @property
    def orient(self):                           # 0 none, 1 flip-x, 2 flip-y, 3 both
        return (self.flags >> 9) & 3


class Timinfo:
    def __init__(self, b):
        if len(b) < 0x10:
            raise StreamError("short timinfo")
        self.pad, cnt, self.tex_x, self.tex_y, self.clut_x, self.clut_y = struct.unpack_from("<6H", b, 0)
        self.reloc = bool(cnt & 0x8000)
        self.count = cnt & 0x7FFF
        if 0xC + 2 * self.count > len(b):
            raise StreamError("timinfo offsets overrun")
        self.frames = []
        for i in range(self.count):
            off = struct.unpack_from("<H", b, 0xC + 2 * i)[0]
            if off + 4 > len(b):
                raise StreamError("timinfo table offset out of range")
            n = struct.unpack_from("<H", b, off)[0]
            if off + 4 + 12 * n > len(b):
                raise StreamError("timinfo table overruns payload")
            spr = []
            for j in range(n):
                s = Sprite()
                s.x, s.y, s.u, s.v, s.clut, s.w, s.h, s.flags = struct.unpack_from("<hhBBHBBH", b, off + 4 + 12 * j)
                if self.reloc:                  # relocation pass
                    s.u = (s.u + (((self.tex_x & 0x3F) << 2) if (s.flags >> 7) & 3 == 0 else ((self.tex_x & 0x3F) << 1))) & 255
                    s.v = (s.v + (self.tex_y & 0xFF)) & 255
                spr.append(s)
            self.frames.append(spr)


# ------------------------------------------------------------------ file access
class Files:
    def __init__(self, game):
        self.zip = zipfile.ZipFile(os.path.join(game, "data", "cdrom.dat"))
        self.names = set(self.zip.namelist())

    def rooms(self):
        return sorted(n[len(MAPDIR):-4] for n in self.names if n.startswith(MAPDIR) and n.endswith(".prd"))

    def drp(self, path):
        return cont.parse_drp(self.zip.read(path))


def load_tim(data):
    if cc_lzss.is_sszl(data):
        data = cc_lzss.decode(data)
    return cc_tim.parse_tim(data)


def tim_footprint(e):
    t = load_tim(e.data)
    x, y, w, h, _ = t.image
    return t, (x, y, w, h)


def build_vram(entries_list):
    vram = cc_tim.Vram()
    for entries in entries_list:
        for e in entries:
            if e.type == 4:
                vram.blit_tim(load_tim(e.data))
    return vram


def slice_rgba(vram, s):
    """RGBA bytes of one sprite slice cut from VRAM (4bpp/8bpp via the CLUT)."""
    cx, cy = (s.clut & 0x3F) * 16, s.clut >> 6
    ncol = 16 if s.depth == 0 else 256
    pal = [cc_tim.bgr555_to_rgba(vram.px[cy * cc_tim.VRAM_W + cx + i]) for i in range(ncol)]
    out = bytearray()
    for j in range(s.h):
        row = (s.page_y + s.v + j) & 511
        for i in range(s.w):
            tx = s.u + i
            if s.depth == 0:
                hw = vram.px[row * cc_tim.VRAM_W + ((s.page_x + (tx >> 2)) & 1023)]
                idx = (hw >> ((tx & 3) * 4)) & 15
            else:
                hw = vram.px[row * cc_tim.VRAM_W + ((s.page_x + (tx >> 1)) & 1023)]
                idx = (hw >> ((tx & 1) * 8)) & 255
            out += bytes(pal[idx])
    return bytes(out)


# ------------------------------------------------------------------ commands
def _room_entries(files, room):
    path = MAPDIR + room + ".prd"
    if path not in files.names:
        raise SystemExit("no .prd for room %r" % room)
    return files.drp(path)


def cmd_info(files, room):
    ents = _room_entries(files, room)
    print("%s.prd: %d entries" % (room, len(ents)))
    for e in ents:
        print("[%d] %-4s type 0x%02X len %d" % (e.index, e.name, e.type, len(e.data)))
        if e.type == 0x0A:
            hd, cmds = parse_effect(e.data)
            start = "stopped" if hd["flags"] & 4 else "running"
            print("   start_delay=%d duration=%d period=%d repeat=%d hdrflags=0x%X (initial %s; jitter=%d)"
                  % (hd["start"], hd["duration"], hd["period"], hd["repeat"], hd["flags"], start,
                     (hd["flags"] >> 3) & 0x3F))
            for c in cmds:
                extra = ""
                if c.op == 0x06:
                    extra = "   ; " + flags_text(dict(c.fields)["flags"])
                print("   " + str(c) + extra)
        elif e.type == 0x03:
            t = Timinfo(e.data)
            print("   tex=(%d,%d) clut=(%d,%d) frames=%d reloc_pending=%s" %
                  (t.tex_x, t.tex_y, t.clut_x, t.clut_y, t.count, t.reloc))
            for k, fr in enumerate(t.frames):
                for j, s in enumerate(fr):
                    print("   f%d s%d: pos=(%d,%d) uv=(%d,%d) size=%dx%d clut=(%d,%d) page=(%d,%d) depth=%dbpp abr=%d "
                          "orient=%d flags=0x%04X" % (k + 1, j, s.x, s.y, s.u, s.v, s.w, s.h, (s.clut & 63) * 16,
                                                      s.clut >> 6, s.page_x, s.page_y, (4, 8, 16, 0)[s.depth], s.abr,
                                                      s.orient, s.flags))
        elif e.type == 0x04:
            t, fp = tim_footprint(e)
            print("   TIM image=(%d,%d) %dx%d halfwords%s" % (fp + ((" clut=(%d,%d)" % t.clut[:2]) if t.clut else "",)))
        elif e.type == 0x10:
            for off, op, wait, name in parse_sequence(e.data):
                print("   %04X %-14s wait_after=%d %s" % (off, SEQ_OPS[op][0], wait, name or ""))


def _resolve(files, room_ents, lib_ents, name, typ):
    for ents in (room_ents, lib_ents):
        for e in ents:
            if e.name == name and e.type == typ:
                return e
    return None


def cmd_validate(files, quiet=False):
    lib = files.drp(PARTICLE) if PARTICLE in files.names else []
    rooms = files.rooms()
    st = collections.Counter()
    ops = collections.Counter()
    sops = collections.Counter()
    fails = []
    unresolved = collections.Counter()
    for room in rooms:
        try:
            ents = files.drp(MAPDIR + room + ".prd")
        except Exception as ex:                       # noqa: BLE001
            fails.append((room, "drp: %s" % ex))
            continue
        st["files"] += 1
        tims = {}
        for e in ents:
            st["entry_0x%02X" % e.type] += 1
            try:
                if e.type == 0x0A:
                    hd, cmds = parse_effect(e.data)
                    for c in cmds:
                        ops[c.op] += 1
                        if c.op == 5:
                            f = dict(c.fields)
                            if f["cache"] != 0:
                                raise StreamError("SPRITE cache not zero")
                            if _resolve(files, ents, lib, f["name"], f["type"] if f["type"] != 14 else 2) is None:
                                unresolved[f["type"]] += 1
                            else:
                                st["sprite_resolved_t%d" % f["type"]] += 1
                    if not any(c.op == 5 for c in cmds):
                        raise StreamError("no SPRITE command")
                elif e.type == 0x03:
                    t = Timinfo(e.data)
                    pair = next((x for x in ents if x.type == 4 and x.name == e.name), None)
                    if pair is None:
                        st["timinfo_unpaired"] += 1
                    else:
                        tm, (ix, iy, iw, ih) = tim_footprint(pair)
                        ok = (ix, iy) == (t.tex_x, t.tex_y) and (tm.clut is None or tm.clut[:2] == (t.clut_x, t.clut_y))
                        st["timinfo_pair_ok" if ok else "timinfo_pair_mismatch"] += 1
                        if not ok:
                            fails.append((room, "timinfo %s tex/clut != TIM (%d,%d) vs (%d,%d)" %
                                          (e.name, t.tex_x, t.tex_y, ix, iy)))
                        for fr in t.frames:
                            for s in fr:
                                tw = iw * (4 if s.depth == 0 else 2)
                                x0 = s.page_x * (4 if s.depth == 0 else 2) + s.u - ix * (4 if s.depth == 0 else 2)
                                y0 = s.page_y + s.v - iy
                                st["sprite_total"] += 1
                                if 0 <= x0 and x0 + s.w <= tw and 0 <= y0 and y0 + s.h <= ih:
                                    st["sprite_inside_tim"] += 1
                    st["timinfo_frames"] += t.count
                elif e.type == 0x10:
                    for _, op, _, _ in parse_sequence(e.data):
                        sops[op] += 1
            except Exception as ex:                   # noqa: BLE001
                fails.append((room, "%s/0x%02X: %s" % (e.name, e.type, ex)))
    print("room .prd files: %d" % st["files"])
    for k in sorted(st):
        if k != "files":
            print("  %-26s %d" % (k, st[k]))
    print("0x0A command histogram:", {"%02X" % k: v for k, v in sorted(ops.items())})
    print("0x10 command histogram:", {"%02X" % k: v for k, v in sorted(sops.items())})
    print("SPRITE names not found in the room .prd or %s (by type): %s" % (PARTICLE, dict(unresolved)))
    print("failures: %d" % len(fails))
    for f in fails[:40]:
        print("  ", f)
    return 0 if not fails else 1


def write_png_rgba(path, w, h, rgba):
    cc_tim.write_png(path, w, h, rgba)


def cmd_render(files, room, outdir):
    ents = _room_entries(files, room)
    lib = files.drp(PARTICLE) if PARTICLE in files.names else []
    vram = build_vram([lib, ents])          # room entries uploaded last (win on overlap)
    os.makedirs(outdir, exist_ok=True)
    n = 0
    for e in ents:
        if e.type != 0x03:
            continue
        t = Timinfo(e.data)
        for k, fr in enumerate(t.frames):
            if not fr:
                continue
            x0, y0 = min(s.x for s in fr), min(s.y for s in fr)
            x1, y1 = max(s.x + s.w for s in fr), max(s.y + s.h for s in fr)
            cw, ch = x1 - x0, y1 - y0
            canvas = bytearray(cw * ch * 4)
            for j, s in enumerate(fr):
                rgba = slice_rgba(vram, s)
                write_png_rgba(os.path.join(outdir, "%s_%s_f%d_s%d.png" % (room, e.name, k + 1, j)), s.w, s.h, rgba)
                n += 1
                for yy in range(s.h):
                    for xx in range(s.w):
                        sx = s.w - 1 - xx if s.orient & 1 else xx
                        sy = s.h - 1 - yy if s.orient & 2 else yy
                        px = rgba[(sy * s.w + sx) * 4:(sy * s.w + sx) * 4 + 4]
                        if px[3]:
                            d = ((s.y - y0 + yy) * cw + (s.x - x0 + xx)) * 4
                            canvas[d:d + 4] = px
            write_png_rgba(os.path.join(outdir, "%s_%s_f%d.png" % (room, e.name, k + 1)), cw, ch, bytes(canvas))
    print("wrote %d slice PNGs (+ composed frames) to %s" % (n, outdir))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", required=True, help="RDE install dir (contains data/cdrom.dat)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("info"); p.add_argument("room")
    sub.add_parser("validate")
    p = sub.add_parser("render"); p.add_argument("room"); p.add_argument("outdir")
    a = ap.parse_args(argv)
    f = Files(a.game)
    if a.cmd == "info":
        cmd_info(f, a.room)
    elif a.cmd == "validate":
        return cmd_validate(f)
    else:
        cmd_render(f, a.room, a.outdir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
