#!/usr/bin/env python
"""Chrono Cross battle stage (battlefield) reader: battle/bg/bg_NN.prd.

Python reference prototype for the C++ importer.

CLI (run with -I):
  python -I cc_stage.py info <bg_NN | path>
  python -I cc_stage.py export <bg_NN | path> <outdir>
  python -I cc_stage.py validate
<bg_NN> is a name like bg_07 (or battle/bg/bg_07.prd); a path on disk also works.
"""
import json
import os
import struct
import sys
import collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # works under python -I

from cc_model_util import Archive, drp_entries, s16, u16, u32
import cc_tim

T_MESH, T_TIM, T_WAVE, T_EMIT, T_CAM, T_SEQ, T_RING, T_LENZ, T_AKAO = 0x02, 0x04, 0x05, 0x0A, 0x0C, 0x10, 0x12, 0x15, 0x16
TYPE_NAMES = {0x02: "mesh", 0x04: "tim", 0x05: "wave(akao)", 0x0A: "emitter", 0x0C: "camera", 0x10: "sequence",
              0x12: "mesh_ring", 0x15: "lenz", 0x16: "akao_se", 0x07: "unk07", 0x18: "unk18"}
FACE_SIZES = {}                       # filled by face_layout()


class FormatError(Exception):
    pass


# --------------------------------------------------------------------------
# DRP + stage container
# --------------------------------------------------------------------------
class Entry:
    def __init__(self, idx, name, typ, data):
        self.idx, self.name, self.type, self.data = idx, name, typ, data


def stage_path(arg):
    a = arg.replace("\\", "/")
    if os.path.isfile(arg):
        return None
    if not a.startswith("battle/"):
        a = "battle/bg/" + a
    if not a.endswith(".prd"):
        a += ".prd"
    return a


def load_bytes(arc, arg):
    p = stage_path(arg)
    if p is None:
        with open(arg, "rb") as f:
            return f.read()
    return arc.read(p)


def parse_stage_entries(d):
    if d[:4] != b"drp\0":
        raise FormatError("not a drp")
    out = []
    for i, (name, typ, s, e) in enumerate(drp_entries(d)):
        if e > len(d) or s > e:
            raise FormatError("entry %d out of range" % i)
        out.append(Entry(i, name, typ, d[s:e]))
    return out


# --------------------------------------------------------------------------
# Mesh (types 0x02 and 0x12) - section 3 of the spec
# --------------------------------------------------------------------------
class Face:
    __slots__ = ("code", "idx", "colors", "uv", "clut", "tpage")

    def __init__(self, code, idx, colors, uv, clut, tpage):
        self.code, self.idx, self.colors, self.uv, self.clut, self.tpage = code, idx, colors, uv, clut, tpage

    quad = property(lambda s: bool(s.code & 8))
    textured = property(lambda s: bool(s.code & 4))
    gouraud = property(lambda s: bool(s.code & 0x10))


def face_size(typ):
    quad, tex, gour = bool(typ & 8), bool(typ & 4), bool(typ & 0x10)
    n = 4 if quad else 3
    return 4 * (n if gour else 1) + ((12 if quad else 10) if tex else 0) + 2 * n + (0 if (tex or quad) else 2)


def parse_mesh(d):
    """Returns (verts[(x,y,z)], faces[Face]).  Raises FormatError on any inconsistency."""
    if len(d) < 0x18 or u32(d, 0) != 0x80000001:
        raise FormatError("mesh magic")
    nv, nf = u16(d, 4), u16(d, 6)
    vo, fo, go = u32(d, 8), u32(d, 12), u32(d, 16)
    if go != 0x14:
        raise FormatError("group header offset %#x" % go)
    if vo + 8 * nv != len(d):
        raise FormatError("vertex array does not end at EOF (%d+8*%d != %d)" % (vo, nv, len(d)))
    verts = [struct.unpack_from("<3h", d, vo + 8 * i) for i in range(nv)]
    ng = u32(d, go)
    if fo != go + 4 + 8 * ng:
        raise FormatError("face data does not follow the group table")
    faces, expect = [], fo
    for g in range(ng):
        typ, cnt, off = u16(d, go + 4 + 8 * g), u16(d, go + 6 + 8 * g), u32(d, go + 8 + 8 * g)
        if typ not in (0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C):
            raise FormatError("face type %#x" % typ)
        if off != expect:
            raise FormatError("group %d not contiguous" % g)
        sz = face_size(typ)
        quad, tex, gour = bool(typ & 8), bool(typ & 4), bool(typ & 0x10)
        n = 4 if quad else 3
        for i in range(cnt):
            p = off + i * sz
            if p + sz > vo:
                raise FormatError("face overruns")
            ncol = n if gour else 1
            cols = [tuple(d[p + 4 * k:p + 4 * k + 3]) for k in range(ncol)]
            if (d[p + 3] & ~2) != typ:
                raise FormatError("code byte %#x != group type %#x" % (d[p + 3], typ))
            q = p + 4 * ncol
            uv, clut, tpage = None, None, None
            if tex:
                uv, clut, tpage = [], 0, 0
                uv.append((d[q], d[q + 1]))
                clut = u16(d, q + 2)
                uv.append((d[q + 4], d[q + 5]))
                tpage = u16(d, q + 6)
                uv.append((d[q + 8], d[q + 9]))
                if quad:
                    uv.append((d[q + 10], d[q + 11]))
                q += 12 if quad else 10
            idx = [u16(d, q + 2 * k) for k in range(n)]
            if n == 3 and tex:  # textured triangles: corner k (uv, colour) belongs to vertex idx[(k+1) % 3] (renderers load idx1, idx2, idx0)
                idx = [idx[1], idx[2], idx[0]]
            if any(x >= nv for x in idx):
                raise FormatError("vertex index out of range")
            faces.append(Face(typ, idx, cols, uv, clut, tpage))
        expect = off + cnt * sz
    if expect != vo:
        raise FormatError("face data ends at %d, vertex array at %d" % (expect, vo))
    if len(faces) != nf:
        raise FormatError("face count %d != header %d" % (len(faces), nf))
    return verts, faces


def decode_tpage(t):
    return {"x": (t & 15) * 64, "y": ((t >> 4) & 1) * 256 + ((t >> 11) & 1) * 512,
            "mode": (t >> 7) & 3, "abr": (t >> 5) & 3}


def decode_clut(c):
    return ((c & 0x3F) << 4, c >> 6)


# --------------------------------------------------------------------------
# Command streams (types 0x10 sequence, 0x0A emitter code): word = op | len<<9 | jump<<15
# --------------------------------------------------------------------------
PAD_21 = bytes([0x21, 0x10, 0, 0, 0, 0, 0, 0])   # duplicate terminator seen after the real 0x21
SEQ_START = 0x14      # payload offset of the stream in a type 0x10 entry (ptr@0x10 = payload+0x14)


def parse_stream(p, start):
    out, o = [], start
    while o + 4 <= len(p):
        w = u32(p, o)
        op, ln = w & 0x1FF, (w >> 9) & 0x3F
        if ln < 4 or o + ln > len(p):
            raise FormatError("bad command length %d at %#x" % (ln, o))
        out.append((o, op, ln, bool(w >> 15 & 1), bytes(p[o + 4:o + ln])))
        o += ln
        if op == 0x21:
            break
    return out, o


def cmd_name(c):
    """4-char ASCII name at command offset +0xC (camera 0x23, entry-run 0x22, sound 0x4C), or None."""
    b = c[4][8:12]
    if len(b) == 4 and b[0] and all(0x20 <= x < 0x7F or x == 0 for x in b):
        return b.split(b"\0")[0].decode("ascii")
    return None


# --------------------------------------------------------------------------
# Camera (type 0x0C)
# --------------------------------------------------------------------------
def parse_camera(p):
    """20-byte header + count x 20-byte keyframes."""
    if len(p) < 20:
        raise FormatError("camera too short")
    count = u32(p, 16)
    if 20 + 20 * count != len(p):
        raise FormatError("camera size %d != 20+20*%d" % (len(p), count))
    hdr = {"flags": u32(p, 0), "w4": u32(p, 4), "w8": u32(p, 8), "ref": tuple(p[12:16])}
    keys = []
    for i in range(count):
        o = 20 + 20 * i
        keys.append({"w0": u32(p, o), "eye": [s16(p, o + 4), s16(p, o + 6), s16(p, o + 8)], "h": s16(p, o + 10),
                     "target": [s16(p, o + 12), s16(p, o + 14), s16(p, o + 16)], "roll": s16(p, o + 18)})
    return hdr, keys


# --------------------------------------------------------------------------
# Lens (type 0x15) - same container as the room "timinfo" (0x03)
# --------------------------------------------------------------------------
def parse_lenz(p):
    n = u16(p, 2) & 0x7FFF
    offs = [u16(p, 0x0C + 2 * i) for i in range(n)]
    tabs = []
    for o in offs:
        if o + 4 > len(p):
            raise FormatError("lenz table offset")
        cnt = u16(p, o)
        ents = []
        for k in range(cnt):
            q = o + 4 + 12 * k
            if q + 12 > len(p):
                raise FormatError("lenz entry overruns")
            x, y, u, v, clut, w, h, m, fl = struct.unpack_from("<hhBBHBBBB", p, q)
            ents.append({"x": x, "y": y, "u": u, "v": v, "clut": clut, "w": w, "h": h, "m": m, "flags": fl})
        tabs.append(ents)
    return tabs


# --------------------------------------------------------------------------
# .ctd companion (raw VRAM sections), present for bg_86..90, bg_99, kamei
# --------------------------------------------------------------------------
def parse_ctd(d):
    """Sector-structured upload list.  Header sector: u32 type (0x1201 CLUT rows / 0x1200 image), u16 n1,n2,n3,n4
    (VRAM x = n1+n3, y = n2+n4), u16 halfwords_per_row, 3 x u16 (rows-in-first-sector, 1, ...), u32 section_count,
    u32 data_sectors, u16 rows_in_sector[data_sectors]; the data sectors follow, rows never straddle a sector."""
    out, sec = [], 0
    while sec * 2048 < len(d):
        o = sec * 2048
        typ = u32(d, o)
        x, y = u16(d, o + 4) + u16(d, o + 8), u16(d, o + 6) + u16(d, o + 10)
        hw, nsec = u16(d, o + 12), u32(d, o + 24)
        if typ not in (0x1200, 0x1201) or hw == 0 or o + 2048 * (1 + nsec) > len(d):
            raise FormatError("bad ctd section header at sector %d" % sec)
        rows = [u16(d, o + 28 + 2 * i) for i in range(nsec)]
        px = bytearray()
        for i, r in enumerate(rows):
            sd = o + 2048 * (1 + i)
            px += d[sd:sd + r * hw * 2]
        out.append({"type": typ, "x": x, "y": y, "w": hw, "h": sum(rows), "data": bytes(px), "sectors": nsec,
                    "count": u32(d, o + 20)})
        sec += 1 + nsec
    if out and out[0]["count"] != len(out):
        raise FormatError("ctd section count %d != %d" % (out[0]["count"], len(out)))
    return out


# --------------------------------------------------------------------------
# Textures
# --------------------------------------------------------------------------
class TexInfo:
    def __init__(self, name, tim, raw_len):
        self.name, self.tim, self.raw_len = name, tim, raw_len


def load_tims(entries):
    out = []
    for e in entries:
        if e.type == T_TIM:
            t = cc_tim.parse_tim(e.data)
            out.append(TexInfo(e.name, t, len(e.data)))
    return out


def build_vram(tims, ctd=None):
    from array import array
    v = cc_tim.Vram()
    for c in (ctd or []):
        a = array("H")
        a.frombytes(c["data"])
        v.blit(c["x"], c["y"], c["w"], c["h"], a)
    for t in tims:
        v.blit_tim(t.tim)
    return v


def tim_rects(t):
    """((x,y,w,h) image rect in halfwords, clut rect or None)"""
    ix, iy, iw, ih, _ = t.tim.image
    return (ix, iy, iw, ih), (t.tim.clut[:4] if t.tim.clut else None)


def atlas(vram, mode, clutx, cluty, x0hw, y0, whw, h):
    """RGBA atlas of VRAM region (halfword x0hw.., rows y0..y0+h) decoded with a CLUT; mode 0 = 4bpp, 1 = 8bpp."""
    ppw = 4 if mode == 0 else 2
    W = whw * ppw
    ncol = 16 if mode == 0 else 256
    pal = [cc_tim.bgr555_to_rgba(c) for c in vram.px[cluty * 1024 + clutx:cluty * 1024 + clutx + ncol]]
    out = bytearray(W * h * 4)
    for y in range(h):
        base = (y0 + y) * 1024 + x0hw
        for i in range(whw):
            v = vram.px[base + i]
            if mode == 1:
                px = (v & 255, v >> 8)
            else:
                px = (v & 15, v >> 4 & 15, v >> 8 & 15, v >> 12)
            for k, ci in enumerate(px):
                o = ((y * W) + i * ppw + k) * 4
                out[o:o + 4] = bytes(pal[ci])
    return W, h, bytes(out)


# --------------------------------------------------------------------------
# Stage object
# --------------------------------------------------------------------------
class Stage:
    def __init__(self, d, name="?", ctd=None):
        self.name = name
        self.ctd = parse_ctd(ctd) if ctd else []
        self.entries = parse_stage_entries(d)
        self.meshes = []        # (entry, verts, faces)
        for e in self.entries:
            if e.type in (T_MESH, T_RING):
                v, f = parse_mesh(e.data)
                self.meshes.append((e, v, f))
        self.tims = load_tims(self.entries)
        self.vram = build_vram(self.tims, self.ctd)
        self.cameras = [(e, ) + parse_camera(e.data) for e in self.entries if e.type == T_CAM]
        self.seqs = [(e, parse_stream(e.data, SEQ_START)) for e in self.entries if e.type == T_SEQ]
        self.lenz = [(e, parse_lenz(e.data)) for e in self.entries if e.type == T_LENZ]

    def emitter_mesh_ref(self, e):
        """Mesh name referenced by an emitter script: a 4-char token in the payload equal to a mesh name."""
        names = {m[0].name for m in self.meshes}
        p = e.data
        for o in range(0x18, len(p) - 3):
            tok = p[o:o + 4]
            if tok[0] and tok.rstrip(b"\0").isalnum() or tok[:3].isalnum() and tok[3:] == b"_":
                s = tok.split(b"\0")[0].decode("latin1")
                if s in names:
                    return s
        return None


def open_stage(arc, arg):
    d = load_bytes(arc, arg)
    ctd = None
    p = stage_path(arg)
    try:
        if p is None:
            c = os.path.splitext(arg)[0] + ".ctd"
            ctd = open(c, "rb").read() if os.path.isfile(c) else None
        else:
            c = p[:-4] + ".ctd"
            ctd = arc.read(c) if c in arc.names else None
    except KeyError:
        ctd = None
    return Stage(d, arg, ctd)


# --------------------------------------------------------------------------
# info
# --------------------------------------------------------------------------
def tex_summary(st):
    pages = collections.Counter()
    cluts = collections.Counter()
    for _, v, f in st.meshes:
        for fc in f:
            if fc.textured:
                tp = decode_tpage(fc.tpage)
                pages[(tp["mode"], tp["x"], tp["y"])] += 1
                cluts[decode_clut(fc.clut)] += 1
    return pages, cluts


def info(arc, arg):
    st = open_stage(arc, arg)
    print("stage %s: %d drp entries" % (arg, len(st.entries)))
    cnt = collections.Counter(e.type for e in st.entries)
    for t, c in sorted(cnt.items()):
        print("  type 0x%02X %-10s x%d: %s" % (t, TYPE_NAMES.get(t, "?"), c,
                                                " ".join(e.name for e in st.entries if e.type == t)[:150]))
    print("meshes:")
    for e, v, f in st.meshes:
        xs = [p[0] for p in v] or [0]
        ys = [p[1] for p in v] or [0]
        zs = [p[2] for p in v] or [0]
        kinds = collections.Counter(fc.code for fc in f)
        print("  %-5s type 0x%02X verts %4d faces %4d  bbox x[%d,%d] y[%d,%d] z[%d,%d]  packets %s"
              % (e.name, e.type, len(v), len(f), min(xs), max(xs), min(ys), max(ys), min(zs), max(zs),
                 ",".join("%02X:%d" % kv for kv in sorted(kinds.items()))))
    pages, cluts = tex_summary(st)
    print("texture pages referenced (mode,x,y):", dict(pages))
    print("cluts referenced (x,y):", dict(cluts))
    print("TIMs (name flag image(x,y,w_hw,h) clut(x,y,w,h)):")
    for t in st.tims:
        ir, cr = tim_rects(t)
        print("  %-5s %dbpp image=%s clut=%s %d bytes" % (t.name, t.tim.bpp, ir, cr, t.raw_len))
    if st.ctd:
        print("ctd sections (type x y w_hw h sectors): " + "; ".join(
            "%#x %d,%d %dx%d %d" % (c["type"], c["x"], c["y"], c["w"], c["h"], c["sectors"]) for c in st.ctd))
    print("cameras (type 0x0C):")
    for e, hdr, keys in st.cameras:
        print("  %-5s flags=%#x w4=%#x ref=%s keys=%d" % (e.name, hdr["flags"], hdr["w4"], hdr["ref"], len(keys)))
        for k in keys:
            print("      eye=%s target=%s h=%d roll=%d w0=%d" % (k["eye"], k["target"], k["h"], k["roll"], k["w0"]))
    print("sequences (type 0x10):")
    for e, (cmds, end) in st.seqs:
        desc = []
        for c in cmds:
            nm = cmd_name(c)
            desc.append("%02X%s" % (c[1], "(%s)" % nm if nm else ""))
        print("  %-5s %d cmds: %s%s" % (e.name, len(cmds), " ".join(desc), "" if end == len(e.data) else
                                       " [+%d trailing]" % (len(e.data) - end)))
    print("emitters (type 0x0A):")
    for e in st.entries:
        if e.type == T_EMIT:
            print("  %-5s %3d bytes -> mesh %s" % (e.name, len(e.data), st.emitter_mesh_ref(e)))
    for e, tabs in st.lenz:
        print("lenz %s: %d tables, sprites per table %s" % (e.name, len(tabs), [len(t) for t in tabs]))
    for e in st.entries:
        if e.type in (T_AKAO, T_WAVE):
            print("akao %s %-5s %d bytes (%s)" % ("wave" if e.type == T_WAVE else "se", e.name, len(e.data),
                                                  e.data[:4].decode("latin1")))
    return 0


# --------------------------------------------------------------------------
# export
# --------------------------------------------------------------------------
def mesh_to_obj(name, verts, faces, x0, whw, matlib):
    """Returns OBJ text.  y is negated (PS1 +y is down) and winding reversed to stay right-handed."""
    lines = ["# part %s  verts %d faces %d" % (name, len(verts), len(faces)), "mtllib %s.mtl" % "stage", "o %s" % name]
    vmap, vlines, vtlines = {}, [], []
    flines = []
    cur_mat = None
    vtn = 0
    for fc in faces:
        n = 4 if fc.quad else 3
        gour = fc.gouraud
        corner = []
        for k in range(n):
            col = fc.colors[k] if gour else fc.colors[0]
            if fc.textured:
                tp = decode_tpage(fc.tpage)
                cx, cy = decode_clut(fc.clut)
                key = (tp["mode"], cx, cy)
                ppw = 4 if tp["mode"] == 0 else 2
                u = (tp["x"] - x0) * ppw + fc.uv[k][0]
                v = (tp["y"] - 256) + fc.uv[k][1]
                matlib.add(key)
                mat = "m_%d_%d_%d" % key
                uvi = (u, v, whw * ppw)
            else:
                mat, uvi = "untextured", None
            vk = (fc.idx[k], col, uvi)
            if vk not in vmap:
                vmap[vk] = len(vmap) + 1
                x, y, z = verts[fc.idx[k]]
                vlines.append("v %d %d %d %.4f %.4f %.4f" % (x, -y, z, col[0] / 128.0, col[1] / 128.0, col[2] / 128.0))
                if uvi is not None:
                    vtlines.append("vt %.6f %.6f" % (uvi[0] / uvi[2], 1.0 - uvi[1] / 256.0))
                else:
                    vtlines.append("vt 0 0")
            corner.append((vmap[vk], mat))
        if corner[0][1] != cur_mat:
            cur_mat = corner[0][1]
            flines.append("usemtl %s" % cur_mat)
        order = [0, 1, 2] if n == 3 else [0, 1, 2, 2, 1, 3]
        for t in range(0, len(order), 3):
            tri = [corner[order[t + j]][0] for j in (0, 2, 1)]       # reversed winding (y flipped)
            flines.append("f " + " ".join("%d/%d" % (i, i) for i in tri))
    return lines + vlines + vtlines + flines, len(vmap)


def export(arc, arg, outdir):
    st = open_stage(arc, arg)
    os.makedirs(outdir, exist_ok=True)
    ext = [(tim_rects(t)[0][0], tim_rects(t)[0][0] + tim_rects(t)[0][2]) for t in st.tims]
    ext += [(c["x"], c["x"] + c["w"]) for c in st.ctd if c["type"] == 0x1200]
    x0 = (min(a for a, b in ext) // 64) * 64 if ext else 0
    whw = (max(b for a, b in ext) - x0) if ext else 384
    mats = set()

    class ML:
        add = staticmethod(mats.add)
    objs = []
    for e, v, f in st.meshes:
        lines, nv = mesh_to_obj(e.name, v, f, x0, whw, ML)
        fn = "part_%02d_%s.obj" % (e.idx, e.name.strip().replace("/", "_"))
        with open(os.path.join(outdir, fn), "w") as fh:
            fh.write(chr(10).join(lines) + chr(10))
        objs.append(fn)
    # texture atlases: one PNG per (mode, clut)
    with open(os.path.join(outdir, "stage.mtl"), "w") as fh:
        fh.write("newmtl untextured\nKd 0.8 0.8 0.8\n")
        for m in sorted(mats):
            png = "tex_m%d_c%d_%d.png" % m
            mode, cx, cy = m
            W, H, rgba = atlas(st.vram, mode, cx, cy, x0, 256, whw, 256)
            cc_tim.write_png(os.path.join(outdir, png), W, H, rgba)
            fh.write("newmtl m_%d_%d_%d\nKd 1 1 1\nmap_Kd %s\n" % (mode, cx, cy, png))
    meta = {
        "stage": arg, "parts": objs, "atlas": {"vram_x0_halfwords": x0, "width_halfwords": whw, "rows": "256..511",
                                              "px_8bpp": [whw * 2, 256]},
        "cameras": [{"name": e.name, "header": {"flags": h["flags"], "w4": h["w4"], "w8": h["w8"], "ref": list(h["ref"])},
                     "keys": k} for e, h, k in st.cameras],
        "sequences": [{"name": e.name, "cmds": [{"off": c[1 - 1], "op": c[1], "len": c[2], "jump": c[3],
                                                 "name": cmd_name(c), "data": c[4].hex()} for c in cmds]}
                      for e, (cmds, end) in st.seqs],
        "emitters": [{"name": e.name, "mesh": st.emitter_mesh_ref(e)} for e in st.entries if e.type == T_EMIT],
        "tims": [{"name": t.name, "bpp": t.tim.bpp, "image": list(tim_rects(t)[0]),
                  "clut": list(tim_rects(t)[1]) if tim_rects(t)[1] else None} for t in st.tims],
    }
    with open(os.path.join(outdir, "stage.json"), "w") as fh:
        json.dump(meta, fh, indent=1)
    print("exported %s -> %s (%d parts, %d atlases)" % (arg, outdir, len(objs), len(mats)))
    return 0


# --------------------------------------------------------------------------
# validate
# --------------------------------------------------------------------------
def validate(arc):
    names = sorted(n for n in arc.names if n.startswith("battle/bg/") and n.endswith(".prd"))
    tot = collections.Counter()
    bad = []
    rows = []
    wid = []
    for n in names:
        try:
            st = open_stage(arc, n)
            probs = []
            nm, nvv, nff = len(st.meshes), 0, 0
            ext = []
            # texture references must land inside the VRAM area covered by this stage's TIMs
            cover = bytearray(1024 * 512)
            clut_rows = set()
            for t in st.tims:
                (ix, iy, iw, ih), cr = tim_rects(t)
                for r in range(iy, min(iy + ih, 512)):
                    cover[r * 1024 + ix:r * 1024 + min(ix + iw, 1024)] = b"" * (min(ix + iw, 1024) - ix)
                if t.tim.clut:
                    clut_rows.add((t.tim.clut[0], t.tim.clut[1]))
                if iy < 256 or iy + ih > 512:
                    probs.append("tim %s outside rows 256..511" % t.name)
                ext.append((ix, ix + iw))
            for c in st.ctd:
                if c["type"] == 0x1201:
                    clut_rows |= {(c["x"], c["y"] + r) for r in range(c["h"])}
                else:
                    for r in range(c["y"], c["y"] + c["h"]):
                        cover[r * 1024 + c["x"]:r * 1024 + c["x"] + c["w"]] = b"" * c["w"]
                    ext.append((c["x"], c["x"] + c["w"]))
                    if c["y"] < 256 or c["y"] + c["h"] > 512:
                        probs.append("ctd image outside rows 256..511")
            for e, v, f in st.meshes:
                nvv += len(v)
                nff += len(f)
                for fc in f:
                    if not fc.textured:
                        continue
                    tp = decode_tpage(fc.tpage)
                    cx, cy = decode_clut(fc.clut)
                    if tp["mode"] not in (0, 1) or tp["y"] != 256:
                        probs.append("%s tpage %#x" % (e.name, fc.tpage))
                        break
                    if (cx, cy) not in clut_rows:
                        probs.append("%s clut (%d,%d) not provided" % (e.name, cx, cy))
                        break
                    ppw = 4 if tp["mode"] == 0 else 2
                    if not all(cover[(tp["y"] + vv) * 1024 + tp["x"] + u // ppw] for u, vv in fc.uv):
                        probs.append("%s uv on unloaded VRAM" % e.name)
                        break
            for e, h, keys in st.cameras:
                if not keys:
                    probs.append("camera %s empty" % e.name)
            for e, (cmds, end) in st.seqs:
                tail = e.data[end:]
                if cmds[-1][1] != 0x21 or (tail and tail != PAD_21):
                    probs.append("sequence %s not terminated by 0x21 at end" % e.name)
            for e, tabs in st.lenz:
                pass
            if ext:
                tot["minx"] = min(tot.get("minx", 9999), min(a for a, b in ext))
                tot["maxx"] = max(tot["maxx"], max(b for a, b in ext))
                wid.append((n, min(a for a, b in ext), max(b for a, b in ext)))
            tot["stages"] += 1
            tot["meshes"] += nm
            tot["verts"] += nvv
            tot["faces"] += nff
            tot["tims"] += len(st.tims)
            tot["cameras"] += len(st.cameras)
            tot["sequences"] += len(st.seqs)
            if probs:
                bad.append((n, probs))
            else:
                tot["ok"] += 1
            rows.append((n, len(st.entries), nm, nvv, nff, len(st.tims), len(st.cameras), len(st.seqs), probs))
        except Exception as ex:
            bad.append((n, ["%s: %s" % (type(ex).__name__, ex)]))
    print("stage                 entries meshes verts faces tims cams seqs")
    for r in rows:
        print("%-22s %6d %6d %6d %5d %4d %4d %4d %s" % (r[0][10:], r[1], r[2], r[3], r[4], r[5], r[6], r[7],
                                                       "; ".join(r[8])[:60]))
    print(dict(tot), "files:", len(names))
    print("VRAM x extent of loaded images (halfwords) histogram:", dict(collections.Counter((a, b) for _, a, b in wid)))
    for n, p in bad:
        print("PROBLEM", n, p[:3])
    return 0 if not bad else 1


def main(argv):
    if len(argv) < 2 or argv[1] not in ("info", "export", "validate"):
        print(__doc__)
        return 2
    arc = Archive()
    if argv[1] == "info":
        return info(arc, argv[2])
    if argv[1] == "export":
        return export(arc, argv[2], argv[3])
    return validate(arc)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
