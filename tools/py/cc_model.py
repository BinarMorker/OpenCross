#!/usr/bin/env python
"""Chrono Cross (RDE data) character / enemy / field-object model reader.

Python reference prototype for the C++ importer.

CLI:
  python -I cc_model.py info <path-or-archive-name>
  python -I cc_model.py export <path-or-archive-name> <outdir>
  python -I cc_model.py validate [--class NAME]

<file> may be a file on disk or a member name inside data/cdrom.dat
(e.g. battle/monster/7star/7star.obj).
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # works under python -I

from cc_model_util import (Archive, cpt_entries, drp_entries, s16, u16, u32)

# --------------------------------------------------------------------------
# Constants
# --------------------------------------------------------------------------
ANGLE_UNIT = 360.0 / 4096.0            # 12-bit angles: 4096 = 360 degrees
S3_SIZE = 0x160                         # attach points + shading + tail
ATTACH_COUNT = 32                       # 32 x (s16 x,y,z ; u16 joint) = 0x100 bytes
SKIN_ONE = 4096                         # blend weights are 12-bit, sum to 4096

# UM (UV map) entry types = PS1 GPU primitive code with the colour byte dropped.
UM_FLAT_TRI, UM_TEX_TRI, UM_FLAT_QUAD, UM_TEX_QUAD = 0x20, 0x24, 0x28, 0x2C
UM_REC_SIZE = {UM_FLAT_TRI: 12, UM_TEX_TRI: 12, UM_FLAT_QUAD: 12, UM_TEX_QUAD: 16}
UM_NVERT = {UM_FLAT_TRI: 3, UM_TEX_TRI: 3, UM_FLAT_QUAD: 4, UM_TEX_QUAD: 4}


class FormatError(Exception):
    pass


# --------------------------------------------------------------------------
# Data classes (plain, so the C++ side can mirror them 1:1)
# --------------------------------------------------------------------------
class Joint:
    __slots__ = ("parent", "rot", "trans", "bone_ref")

    def __init__(self, parent, rot, trans, bone_ref):
        self.parent, self.rot, self.trans, self.bone_ref = parent, rot, trans, bone_ref


class Vertex:
    """One output vertex.  pos/joints/weights have 1 entry (8-byte mode) or 2 (16-byte mode)."""
    __slots__ = ("index", "joints", "weights", "pos")

    def __init__(self, index, joints, weights, pos):
        self.index, self.joints, self.weights, self.pos = index, joints, weights, pos


class Face:
    """kind: UM_* ; v: global vertex indices in UV-record order; uv: [(u,v)] or None; color: (r,g,b) for flat.
    page = OR of all flag bits (legacy); clut_bit = bit 0 of the 1st pointer (UV-record order) -> CLUT row + 1,
    page_bit = bit 0 of the 2nd pointer -> texture page + 1 (u + 128)."""
    __slots__ = ("kind", "v", "uv", "color", "page", "raw", "clut_bit", "page_bit")

    def __init__(self, kind, v, uv, color, page, raw, clut_bit=0, page_bit=0):
        self.kind, self.v, self.uv, self.color = kind, v, uv, color
        self.page, self.raw = page, raw
        self.clut_bit, self.page_bit = clut_bit, page_bit


class Construct:
    def __init__(self):
        self.um = self.vp = self.vd = 0
        self.n8 = self.n16 = 0
        self.first_index = 0
        self.vertices = []
        self.normals = []
        self.faces = []
        self.um_entries = []


class Anim:
    def __init__(self):
        self.frame_count = 0
        self.flags = []       # per joint: (rot_flag, trans_flag)
        self.frames = []      # frames[f] = list over joints of (rot|None, trans|None)
        self.size = 0


class Mdl:
    pass


# --------------------------------------------------------------------------
# Mdl (the .obj of battle/monster/player and field objects)
# --------------------------------------------------------------------------
def parse_mdl(d, name="?"):
    """Header: u32 N(=6) ; u32 S1..S6 ; u32 EOF.  (S6 == 0 if absent.)"""
    m = Mdl()
    m.name = name
    m.size = len(d)
    n = u32(d, 0)
    if n != 6:
        raise FormatError("header count %d != 6" % n)
    m.ptrs = [u32(d, 4 + 4 * i) for i in range(n)]
    m.eof = u32(d, 4 + 4 * n)
    if m.eof != len(d):
        raise FormatError("EOF pointer %#x != size %#x" % (m.eof, len(d)))
    s1, s2, s3, s4, s5, s6 = m.ptrs
    m.s6 = s6
    if not (s1 < s2 < s3 < s4 < s5 <= (s6 or m.eof) <= m.eof):
        raise FormatError("section pointers not monotonic %s" % [hex(p) for p in m.ptrs])
    m.joints = parse_skeleton(d, s2, s3)
    m.constructs = parse_geometry(d, s1, s2, len(m.joints))
    m.attach, m.shading, m.s3_tail = parse_s3(d, s3, s4)
    m.anims = parse_anim_pack(d, s4, s5, len(m.joints))
    m.s5 = parse_s5(d, s5, s6 or m.eof)
    m.drp = None
    if s6:
        m.drp = drp_entries(d[s6:m.eof]) if d[s6:s6 + 4] == b"drp\0" else None
    return m


def parse_skeleton(d, o, end):
    """S2: u32 N ; N x 20-byte joint record (s32 parent, 3 x s16 rot, 3 x s16 trans, u32 bone_ref)."""
    n = u32(d, o)
    if o + 4 + 20 * n != end:
        raise FormatError("S2 size mismatch (N=%d)" % n)
    out = []
    for i in range(n):
        p, rx, ry, rz, tx, ty, tz, bref = struct.unpack_from("<i3h3hI", d, o + 4 + 20 * i)
        if not (p == -1 or 0 <= p < i):
            raise FormatError("joint %d parent %d" % (i, p))
        out.append(Joint(p, (rx, ry, rz), (tx, ty, tz), bref))
    return out


def parse_s3(d, o, end):
    if end - o != S3_SIZE:
        raise FormatError("S3 size %#x" % (end - o))
    attach = [struct.unpack_from("<3hH", d, o + 8 * i) for i in range(ATTACH_COUNT)]
    shading = [tuple(d[o + 0x100 + 4 * i:o + 0x100 + 4 * i + 4]) for i in range(16)]
    tail = bytes(d[o + 0x140:o + 0x160])
    return attach, shading, tail


def parse_s5(d, o, end):
    """S5 (animation-related blocks): u32 N ; (N+1) x u32 offsets.  Kept as raw spans."""
    n = u32(d, o)
    ptrs = [u32(d, o + 4 + 4 * i) for i in range(n + 1)]
    return {"count": n, "ptrs": ptrs, "span": (o, end)}


def parse_geometry(d, s1, s1_end, njoints):
    nc = u32(d, s1)
    offs = [u32(d, s1 + 4 + 4 * i) for i in range(nc)]
    cons = []
    base_index = 0
    for ci, co in enumerate(offs):
        c = Construct()
        o = s1 + co
        c.um, c.vp, c.vd = u32(d, o), u32(d, o + 4), u32(d, o + 8)
        o += 12
        # 8-byte-mode assignments: (#A, VO) then #A x (u16 NV, u16 joint)
        na, vo8 = u32(d, o), u32(d, o + 4)
        o += 8
        a8 = []
        for i in range(na):
            a8.append((u16(d, o), u16(d, o + 2)))
            o += 4
        # 16-byte-mode assignments: (#B, VO) then #B x (u16 NV, u16 0, u16 J1, u16 W1, u16 J2, u16 W2)
        nb, vo16 = u32(d, o), u32(d, o + 4)
        o += 8
        a16 = []
        for i in range(nb):
            nv, z, j1, w1, j2, w2 = struct.unpack_from("<6H", d, o)
            a16.append((nv, j1, w1, j2, w2))
            o += 12
        # UV map footer
        ne = u32(d, o)
        o += 4
        for i in range(ne):
            c.um_entries.append((u16(d, o), u16(d, o + 2), u32(d, o + 4)))
            o += 8
        if ci + 1 < nc and s1 + offs[ci + 1] != o:
            raise FormatError("construct %d footer end %#x != next %#x" % (ci, o, s1 + offs[ci + 1]))
        c.footer_end = o
        c.first_index = base_index
        c.n8 = sum(a[0] for a in a8)
        c.n16 = sum(a[0] for a in a16)
        if vo8 != 0 or vo16 != 8 * c.n8:
            raise FormatError("construct %d pool offsets %#x/%#x (n8=%d)" % (ci, vo8, vo16, c.n8))
        vp = s1 + c.vp
        pos = vp + vo8
        for nv, j in a8:
            for _ in range(nv):
                x, y, z, idx = struct.unpack_from("<3hH", d, pos)
                pos += 8
                if j >= njoints:
                    raise FormatError("vertex joint %d >= %d" % (j, njoints))
                c.vertices.append(Vertex(idx, (j,), (SKIN_ONE,), ((x, y, z),)))
        pos = vp + vo16
        for nv, j1, w1, j2, w2 in a16:
            for _ in range(nv):
                xa, ya, za, ia = struct.unpack_from("<3hH", d, pos)
                xb, yb, zb, ib = struct.unpack_from("<3hH", d, pos + 8)
                pos += 16
                if ia != ib:
                    raise FormatError("16-byte vertex idx mismatch")
                if j1 >= njoints or j2 >= njoints or not (SKIN_ONE - 2 <= w1 + w2 <= SKIN_ONE):
                    raise FormatError("16-byte vertex joints/weights %d,%d %d+%d" % (j1, j2, w1, w2))
                # record order is (position rel. J2, position rel. J1): validated on 4778 seam vertices
                c.vertices.append(Vertex(ia, (j1, j2), (w1, w2), ((xb, yb, zb), (xa, ya, za))))
        c.vp_end = pos
        c.indices_ok = sorted(v.index for v in c.vertices) == list(range(base_index, base_index + len(c.vertices)))
        nrec = c.n8 + 2 * c.n16
        c.normals = [struct.unpack_from("<3bB", d, s1 + c.vd + 4 * i) for i in range(nrec)]
        c.faces = parse_um(d, s1, c)
        base_index += len(c.vertices)
        cons.append(c)
    return cons


def parse_um(d, s1, c):
    """Decode the UV-map entries of one construct into faces with global vertex indices.

    Pointer rule: every face record carries one s16 "pointer" per vertex.  For the first two
    vertices of a textured face (in UV-record order) the stored value is 16 x index, for the
    others (and for all vertices of flat 0x20/0x28 faces) 8 x index.  value >= 0 : absolute global vertex index (remainder = flag bits).
    value < 0 : back-reference; the target is the vertex resolved `-value/unit` slots earlier
    in a flat slot list in which every face (triangles included: 3 + 1 pad) uses 4 slots.
    Textured triangles store their pointers rotated: UV1<-p2, UV2<-p3, UV3<-p1.
    """
    faces = []
    slots = []
    for t, cnt, uo in c.um_entries:
        if t not in UM_REC_SIZE:
            raise FormatError("unknown UM type %#x" % t)
        sz = UM_REC_SIZE[t]
        n = UM_NVERT[t]
        for i in range(cnt):
            p = s1 + c.um + uo + i * sz
            if t in (UM_TEX_TRI, UM_TEX_QUAD):
                uv = [(d[p + 2 * k], d[p + 2 * k + 1]) for k in range(n)]
                ptr_o = p + 2 * n
                color = None
            else:
                uv = None
                color = tuple(d[p:p + 3])
                ptr_o = p + 4
            ptrs = [s16(d, ptr_o + 2 * k) for k in range(n)]
            textured = t in (UM_TEX_TRI, UM_TEX_QUAD)
            ordered = [ptrs[1], ptrs[2], ptrs[0]] if (n == 3 and textured) else ptrs
            res = []
            page = 0
            for k, v in enumerate(ordered):
                unit = 16 if (k < 2 and textured) else 8      # flat primitives: all slots x8
                page |= v % unit              # low bits (below the unit) are flag bits
                if v >= 0:
                    r = v // unit
                else:
                    ref = len(slots) + v // unit      # floor division: -47 = -48 + 1
                    if ref < 0 or slots[ref] is None:
                        raise FormatError("bad back-reference %d at slot %d" % (v, len(slots)))
                    r = slots[ref]
                res.append(r)
                slots.append(r)
            if n == 3:
                slots.append(None)
            cbit = (ordered[0] % 16) & 1 if textured else 0     # CLUT row + 1
            pbit = (ordered[1] % 16) & 1 if textured else 0     # texture page + 1 (u + 128)
            faces.append(Face(t, res, uv, color, page, tuple(ptrs), cbit, pbit))
    return faces


# --------------------------------------------------------------------------
# Animations
# --------------------------------------------------------------------------
def parse_anim(d, start, end, njoints):
    """One animation: u32 F ; flag bits (2 per joint, padded to u32) ; F x u32 frame pointers (rel. to start).

    Frame 0 = 12 bytes per joint (s16 x3 rot, s16 x3 trans) for ALL joints.
    Frame k>0 = 6 bytes per set flag bit, in joint order (rot then trans).
    """
    a = Anim()
    a.size = end - start
    f = u32(d, start)
    a.frame_count = f
    if f == 0 or f > 4000:
        raise FormatError("anim frame count %d" % f)
    if njoints is None:      # external packs: derive J from the frame-0 size
        njoints = infer_joint_count(d, start, end)
    nflag = ((2 * njoints + 31) // 32) * 4
    bits = int.from_bytes(d[start + 4:start + 4 + nflag], "little")
    a.flags = [((bits >> (2 * j)) & 1, (bits >> (2 * j + 1)) & 1) for j in range(njoints)]
    if bits >> (2 * njoints):
        raise FormatError("flag bits beyond joint count")
    ptr_o = start + 4 + nflag
    ptrs = [u32(d, ptr_o + 4 * i) for i in range(f)]
    if ptrs[0] != 4 + nflag + 4 * f:
        raise FormatError("frame 0 pointer %#x != %#x" % (ptrs[0], 4 + nflag + 4 * f))
    bounds = ptrs + [end - start]
    ncmd = sum(r + t for r, t in a.flags)
    for k in range(f):
        s, e = start + bounds[k], start + bounds[k + 1]
        if e < s:
            raise FormatError("frame pointers not monotonic")
        if k == 0:
            if e - s != 12 * njoints:
                raise FormatError("frame 0 size %d != %d" % (e - s, 12 * njoints))
            fr = []
            for j in range(njoints):
                vals = struct.unpack_from("<6h", d, s + 12 * j)
                fr.append((vals[0:3], vals[3:6]))
        else:
            pad = e - s - 6 * ncmd           # only the last frame may carry end padding of the animation
            if not (pad == 0 or (k == f - 1 and 0 < pad < 4)):
                raise FormatError("frame %d size %d != %d" % (k, e - s, 6 * ncmd))
            fr = []
            q = s
            for j in range(njoints):
                rv = tv = None
                if a.flags[j][0]:
                    rv = struct.unpack_from("<3h", d, q)
                    q += 6
                if a.flags[j][1]:
                    tv = struct.unpack_from("<3h", d, q)
                    q += 6
                fr.append((rv, tv))
        a.frames.append(fr)
    return a


def parse_anim_pack(d, s, e, njoints):
    """A cpt of animations (with EOF pointer).  Returns list of Anim."""
    out = []
    for (a, b) in cpt_entries(d, s, e):
        out.append(parse_anim(d, a, b, njoints))
    return out


# --------------------------------------------------------------------------
# Pose math (pure python; used by export and by the validator)
# --------------------------------------------------------------------------
import math


def _mm(A, B):
    return [[sum(A[i][k] * B[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def _mv(A, v):
    return [A[i][0] * v[0] + A[i][1] * v[1] + A[i][2] * v[2] for i in range(3)]


def _rx(a):
    c, s = math.cos(a), math.sin(a)
    return [[1, 0, 0], [0, c, -s], [0, s, c]]


def _ry(a):
    c, s = math.cos(a), math.sin(a)
    return [[c, 0, s], [0, 1, 0], [-s, 0, c]]


def _rz(a):
    c, s = math.cos(a), math.sin(a)
    return [[c, -s, 0], [s, c, 0], [0, 0, 1]]


def joint_rotation(rot):
    """Q = Rz * Ry * Rx (X applied first); angles are 12-bit (4096 = 360 deg)."""
    k = 2 * math.pi / 4096
    return _mm(_rz(rot[2] * k), _mm(_ry(rot[1] * k), _rx(rot[0] * k)))


def pose_matrices(joints, rots, trans):
    """Return per joint (A, b) with  u_root = A*u_joint + b  (A = 3x3 rotation, b = translation).

    Convention [I, validated on 601 seam vertices over 5 models and all single-bone seams]:
        u_parent = R * u_joint + t,   R = Rz(rz) * Ry(ry) * Rx(rx)   (x applied first),
    angles in 12-bit units (4096 = 360 deg).  Vertex coordinates are the raw record order
    (first s16 = x, second = y, third = z); +y is DOWN (PS1 convention).
    """
    W = []
    for i, j in enumerate(joints):
        R = joint_rotation(rots[i])
        t = list(trans[i])
        if j.parent >= 0:
            Ap, bp = W[j.parent]
            W.append((_mm(Ap, R), [x + y for x, y in zip(_mv(Ap, t), bp)]))
        else:
            W.append((R, t))
    return W


def rest_pose(m):
    return pose_matrices(m.joints, [j.rot for j in m.joints], [j.trans for j in m.joints])


def skin_vertices(m, W):
    """Posed vertex positions {global index: (x,y,z)}."""
    P = {}
    for c in m.constructs:
        for v in c.vertices:
            acc = [0.0, 0.0, 0.0]
            for j, w, p in zip(v.joints, v.weights, v.pos):
                A, b = W[j]
                q = _mv(A, p)
                for i in range(3):
                    acc[i] += (q[i] + b[i]) * w / SKIN_ONE
            P[v.index] = acc
    return P


def infer_joint_count(d, start, end):
    """J such that frame0 pointer == 4 + 4*ceil(2J/32) + 4F and frame-0 size == 12 J."""
    f = u32(d, start)
    for nflag_words in range(1, 6):
        p0 = 4 + 4 * nflag_words + 4 * f
        if p0 > end - start:
            break
        nxt = u32(d, start + 4 + 4 * nflag_words + 4) if f > 1 else end - start
        sz = nxt - p0
        if sz > 0 and sz % 12 == 0 and u32(d, start + 4 + 4 * nflag_words) == p0:
            j = sz // 12
            if (2 * j + 31) // 32 == nflag_words:
                return j
    raise FormatError("cannot infer joint count")


# --------------------------------------------------------------------------
# Textures: atim (battle) / TIM (field)
# --------------------------------------------------------------------------
def rgb555(v):
    return ((v & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, ((v >> 10) & 31) * 255 // 31)


def write_png(path, w, h, rgba):
    import zlib
    raw = b"".join(b"\0" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))

    def chunk(t, data):
        c = struct.pack(">I", len(data)) + t + data
        return c + struct.pack(">I", zlib.crc32(t + data) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


class Atim:
    """cpt: [CLUT object, image object, ...].  Object header, 16 bytes:
    u32 type (0x1101 clut / 0x1100 image), u16 vram_x, u16 vram_y, u32 0, u16 w, u16 h
    (clut: w entries x h rows of RGB555; image: w in 16-bit VRAM words = 2 px at 8bpp)."""

    def __init__(self, d, base=0, limit=None):
        ents = cpt_entries(d, base, limit)
        if len(ents) < 2:
            raise FormatError("atim needs >= 2 objects")
        s, e = ents[0]
        if u32(d, s) != 0x1101:
            raise FormatError("atim clut type %#x" % u32(d, s))
        self.clut_w, self.clut_h = u16(d, s + 12), u16(d, s + 14)
        if e - s != 16 + 2 * self.clut_w * self.clut_h:
            raise FormatError("atim clut size")
        self.cluts = []
        for r in range(self.clut_h):
            row = []
            for i in range(self.clut_w):
                v = u16(d, s + 16 + 2 * (r * self.clut_w + i))
                row.append(rgb555(v) + (0 if v == 0 else 255,))
            self.cluts.append(row)
        self.images = []
        for s, e in ents[1:]:
            if u32(d, s) != 0x1100:
                raise FormatError("atim image type %#x" % u32(d, s))
            w, h = u16(d, s + 12) * 2, u16(d, s + 14)
            if e - s != 16 + w * h:
                raise FormatError("atim image size %d != %d" % (e - s, 16 + w * h))
            self.images.append((u16(d, s + 4), u16(d, s + 6), w, h, d[s + 16:e]))

    def rgba(self, image=0, clut_row=0):
        _, _, w, h, px = self.images[image]
        pal = self.cluts[min(clut_row, self.clut_h - 1)]
        out = bytearray()
        for p in px:
            out += bytes(pal[p])
        return w, h, bytes(out)


def parse_atim_file(d):
    """.ltd = one atim.  .ltc = cpt of 2 atims (main texture, small overlay strip)."""
    s0 = u32(d, 4)
    if u32(d, s0) == 0x1101:
        return [Atim(d, 0)]
    return [Atim(d, s, e) for s, e in cpt_entries(d, 0)]


# --------------------------------------------------------------------------
# Mesh (.kmd, weapon packs): PS1-packet style rigid mesh without skeleton
# --------------------------------------------------------------------------
def parse_mesh(d, base, end):
    if u32(d, base) != 0x80000001:
        raise FormatError("mesh magic %#x" % u32(d, base))
    nv, nf = u16(d, base + 4), u16(d, base + 6)
    vo, fo, go = u32(d, base + 8), u32(d, base + 12), u32(d, base + 16)
    verts = [struct.unpack_from("<3hH", d, base + vo + 8 * i) for i in range(nv)]
    ng = u32(d, base + go)
    faces = []
    for g in range(ng):
        typ, cnt, off = u16(d, base + go + 4 + 8 * g), u16(d, base + go + 6 + 8 * g), u32(d, base + go + 8 + 8 * g)
        quad, tex, gour = bool(typ & 8), bool(typ & 4), bool(typ & 0x10)
        nvv = 4 if quad else 3
        ncol = nvv if gour else 1
        size = 4 * ncol + ((12 if quad else 10) if tex else 0) + 2 * nvv + (0 if (tex or quad) else 2)
        for i in range(cnt):
            p = base + off + i * size
            q = p + 4 * ncol + (((12 if quad else 10)) if tex else 0)
            idx = [u16(d, q + 2 * k) for k in range(nvv)]
            if any(x >= nv for x in idx):
                raise FormatError("mesh index out of range")
            faces.append((typ, idx, p))
        if base + off + cnt * size > end:
            raise FormatError("mesh group overruns")
    if len(faces) != nf:
        raise FormatError("mesh face count %d != %d" % (len(faces), nf))
    return verts, faces


def parse_weapon_bin(d):
    ents = cpt_entries(d, 0)
    out = {"tim": None, "meshes": 0, "empty": 0}
    for i, (s, e) in enumerate(ents):
        if e - s <= 4:
            out["empty"] += 1
        elif u32(d, s) == 0x10:            # standard TIM (4bpp + CLUT), one per mesh pair
            out["tim"] = out["tim"] or (s, e)
            out["tims"] = out.get("tims", 0) + 1
        else:
            parse_mesh(d, s, e)
            out["meshes"] += 1
    return out


def parse_anim_cpt(d, s, e, njoints=None):
    return [parse_anim(d, a, b, njoints) for a, b in cpt_entries(d, s, e)]


def parse_ex_bin(d):
    """fieldobj/ex/*.bin: cpt of 2: [anim pack, cpt of opaque script-like blocks]."""
    ents = cpt_entries(d, 0)
    if len(ents) != 2:
        raise FormatError("ex bin with %d entries" % len(ents))
    anims = parse_anim_cpt(d, ents[0][0], ents[0][1])
    blocks = cpt_entries(d, ents[1][0], ents[1][1])
    return anims, blocks


def parse_prd_anims(d):
    """DRP: the type-0x19 entry ('at0') is an anim cpt; type 0x1a ('atk') is something else (not parsed)."""
    out = {}
    for name, typ, s, e in drp_entries(d):
        if typ == 0x19:
            out[name + "/%02x" % typ] = parse_anim_cpt(d, s, e)
    return out


# --------------------------------------------------------------------------
# Field TIM (standard PS1 TIM, 8bpp/4bpp with CLUT)
# --------------------------------------------------------------------------
def parse_tim(d):
    """Returns (w, h, rgba) of a CLUT TIM (first palette row)."""
    if u32(d, 0) != 0x10:
        raise FormatError("TIM magic")
    flag = u32(d, 4)
    bpp = {0: 4, 1: 8}.get(flag & 3)
    if bpp is None or not flag & 8:
        raise FormatError("TIM flag %#x" % flag)
    clen = u32(d, 8)
    cw, ch = u16(d, 8 + 8), u16(d, 8 + 10)
    pal = []
    for i in range(cw):
        v = u16(d, 8 + 12 + 2 * i)
        pal.append(rgb555(v) + (0 if v == 0 else 255,))
    io = 8 + clen
    iw, ih = u16(d, io + 8), u16(d, io + 10)
    w = iw * (4 if bpp == 4 else 2)
    px = d[io + 12:io + 12 + iw * 2 * ih]
    if len(px) != iw * 2 * ih:
        raise FormatError("TIM image truncated")
    out = bytearray()
    for b in px:
        if bpp == 8:
            out += bytes(pal[b if b < len(pal) else 0])
        else:
            out += bytes(pal[(b & 15) % len(pal)]) + bytes(pal[(b >> 4) % len(pal)])
    return w, ih, bytes(out)


# --------------------------------------------------------------------------
# Bundles, export, info, validate
# --------------------------------------------------------------------------
def classify(name):
    n = name.replace("\\", "/")
    ext = n.rsplit(".", 1)[-1]
    if ext == "obj":
        return "mdl"
    return {"ltd": "atim", "ltc": "atim", "kmd": "mesh", "prd": "prd", "tim": "tim"}.get(
        ext, "ex_bin" if "/ex/" in n else "weapon_bin" if "wepon" in n else "?")


def read_any(arc, name):
    import os
    if os.path.exists(name):
        with open(name, "rb") as f:
            return f.read()
    return arc.read(name)


def load_textures(arc, objname):
    """Return (kind, [Atim]) or ('tim', (w,h,rgba)) or None for the model file `objname`."""
    base = objname.rsplit(".", 1)[0]
    names = set(arc.names)
    for ext in (".ltd", ".ltc"):
        if base + ext in names:
            return "atim", parse_atim_file(arc.read(base + ext))
    if base + ".tim" in names:
        return "tim", parse_tim(arc.read(base + ".tim"))
    return None


def anim_to_json(m, an):
    k = ANGLE_UNIT
    frames = []
    for f in an.frames:
        fr = []
        for r, t in f:
            fr.append({"rot_deg": [round(x * k, 4) for x in r] if r is not None else None,
                       "trans": list(t) if t is not None else None})
        frames.append(fr)
    return {"frame_count": an.frame_count, "flags_rot_trans": an.flags, "frames": frames,
            "note": "values are DELTAS added to the skeleton rest rotation/translation; null = unchanged from frame 0"}


def export_model(arc, name, outdir):
    import json
    import os
    os.makedirs(outdir, exist_ok=True)
    d = read_any(arc, name)
    m = parse_mdl(d, name)
    stem = os.path.basename(name).rsplit(".", 1)[0]
    tex = load_textures(arc, name) if name in arc.names else None
    tw = th = None
    if tex:
        if tex[0] == "atim":
            a0 = tex[1][0]
            for row in range(a0.clut_h):
                w, h, rgba = a0.rgba(0, row)
                write_png(os.path.join(outdir, "%s_clut%d.png" % (stem, row)), w, h, rgba)
            for i, ex in enumerate(tex[1][1:], 1):
                w, h, rgba = ex.rgba(0, 0)
                write_png(os.path.join(outdir, "%s_overlay%d.png" % (stem, i)), w, h, rgba)
            tw, th = a0.images[0][2], a0.images[0][3]
        else:
            w, h, rgba = tex[1]
            write_png(os.path.join(outdir, stem + ".png"), w, h, rgba)
            tw, th = w, h
    # OBJ in rest pose (y flipped so +y is up), UVs; page bit -> +128 px (right half); the CLUT bit (second
    # palette row) is recorded per face in the mesh json ("clut"), the OBJ carries one texture only
    P = skin_vertices(m, rest_pose(m))
    order = []
    for c in m.constructs:
        for v in c.vertices:
            order.append(v.index)
    pos_of = {i: n for n, i in enumerate(sorted(order))}
    lines = ["# %s  (rest pose, y up; vertex k bone data in %s.mesh.json)" % (name, stem)]
    for i in sorted(order):
        x, y, z = P[i]
        lines.append("v %.3f %.3f %.3f" % (x, -y, z))
    vt = []
    fl = []
    for c in m.constructs:
        for f in c.faces:
            idx = []
            for k, v in enumerate(f.v):
                if f.uv and tw:
                    u, vv = f.uv[k]
                    u += 128 * f.page_bit
                    vt.append("vt %.5f %.5f" % ((u + 0.5) / tw, 1 - (vv + 0.5) / th))
                    idx.append("%d/%d" % (pos_of[v] + 1, len(vt)))
                else:
                    idx.append("%d" % (pos_of[v] + 1))
            if len(idx) == 4:                     # Z order -> perimeter order
                idx = [idx[0], idx[1], idx[3], idx[2]]
            fl.append("f " + " ".join(idx))
    with open(os.path.join(outdir, stem + ".obj"), "w") as fh:
        fh.write("\n".join(lines + vt + fl) + "\n")
    mesh = {"vertices": [], "faces": []}
    for c in m.constructs:
        for v in c.vertices:
            mesh["vertices"].append({"index": v.index, "joints": list(v.joints),
                                     "weights": [w / SKIN_ONE for w in v.weights],
                                     "pos_local": [list(p) for p in v.pos]})
        for f in c.faces:
            mesh["faces"].append({"kind": hex(f.kind), "v": f.v, "uv": f.uv, "color": f.color, "page": f.page_bit, "clut": f.clut_bit})
    mesh["vertices"].sort(key=lambda v: v["index"])
    with open(os.path.join(outdir, stem + ".mesh.json"), "w") as fh:
        json.dump(mesh, fh)
    sk = {"unit": "rot: 4096 = 360 deg; trans: model units; angle order R = Rz*Ry*Rx",
          "joints": [{"index": i, "parent": j.parent, "rot": list(j.rot),
                      "rot_deg": [round(x * ANGLE_UNIT, 3) for x in j.rot], "trans": list(j.trans), "bone_ref": j.bone_ref}
                     for i, j in enumerate(m.joints)],
          "attach_points": [{"index": i, "pos": [a[0], a[1], a[2]], "joint": a[3]} for i, a in enumerate(m.attach)],
          "shading": [list(s_) for s_ in m.shading]}
    with open(os.path.join(outdir, stem + ".skeleton.json"), "w") as fh:
        json.dump(sk, fh, indent=1)
    with open(os.path.join(outdir, stem + ".anims.json"), "w") as fh:
        json.dump([anim_to_json(m, a) for a in m.anims], fh)
    return m


def info(arc, name):
    d = read_any(arc, name)
    kind = classify(name)
    print("%s  (%d bytes)  kind=%s" % (name, len(d), kind))
    if kind == "mdl":
        m = parse_mdl(d, name)
        print(" sections:", ", ".join("S%d=%#x" % (i + 1, p) for i, p in enumerate(m.ptrs)), "eof=%#x" % m.eof)
        print(" joints: %d   constructs: %d" % (len(m.joints), len(m.constructs)))
        for i, c in enumerate(m.constructs):
            kinds = ", ".join("%#x x%d" % (t, n) for t, n, o in c.um_entries)
            print("  construct %d: verts %d (8-byte %d, 16-byte %d) faces %d [%s]" % (
                i, len(c.vertices), c.n8, c.n16, len(c.faces), kinds))
        tv = sum(len(c.vertices) for c in m.constructs)
        tf = sum(len(c.faces) for c in m.constructs)
        print(" total verts %d faces %d" % (tv, tf))
        print(" anims: %d  frames: %s" % (len(m.anims), [a.frame_count for a in m.anims]))
        print(" S5 blocks: %d   S6 drp: %s" % (m.s5["count"], [e[0] for e in m.drp] if m.drp else None))
        print(" attach (joint ids): %s" % [a[3] for a in m.attach if any(a[:3])])
    elif kind == "atim":
        for a in parse_atim_file(d):
            print(" atim clut %dx%d images %s" % (a.clut_w, a.clut_h, [(i[0], i[1], i[2], i[3]) for i in a.images]))
    elif kind == "mesh":
        v, f = parse_mesh(d, 0, len(d))
        print(" mesh verts %d faces %d" % (len(v), len(f)))
    elif kind == "weapon_bin":
        print(" ", parse_weapon_bin(d))
    elif kind == "ex_bin":
        an, bl = parse_ex_bin(d)
        print(" anim pack: %d anims (J=%d) ; extra blocks %d" % (len(an), len(an[0].flags), len(bl)))
    elif kind == "prd":
        for k, v in parse_prd_anims(d).items():
            print(" %s: %d anims, J=%d" % (k, len(v), len(v[0].flags)))
        print(" entries:", [(n, hex(t)) for n, t, s, e in drp_entries(d)])
    elif kind == "tim":
        w, h, _ = parse_tim(d)
        print(" tim %dx%d" % (w, h))


# ---- validation ------------------------------------------------------------
def validate(arc, only=None):
    import collections
    cl = arc.model_files()
    names = set(arc.names)
    table = []            # (class, ok, total, [anomalies])
    anomalies = collections.defaultdict(list)

    def chk(cls, fn):
        ok = 0
        for n in cl[cls]:
            try:
                fn(n)
                ok += 1
            except Exception as e:
                anomalies[cls].append("%s: %s: %s" % (n, type(e).__name__, e))
        table.append((cls, ok, len(cl[cls])))

    def model_check(n):
        d = arc.read(n)
        m = parse_mdl(d, n)
        tot = sum(len(c.vertices) for c in m.constructs)
        for ci, c in enumerate(m.constructs):
            if not c.indices_ok:
                anomalies["note"].append("%s: construct %d vertex idx not sequential" % (n, ci))
            lo, hi = c.first_index, c.first_index + len(c.vertices)
            for f in c.faces:
                if any(not (lo <= v < hi) for v in f.v):
                    raise FormatError("face references vertex outside its construct")
        tex = load_textures(arc, n)
        if tex is None:
            anomalies["note"].append("%s: no texture sibling" % n)
        elif tex[0] == "atim":
            a0 = tex[1][0]
            imgw, imgh = a0.images[0][2], a0.images[0][3]
            pages = any(f.page & 1 for c in m.constructs for f in c.faces)
            if pages and imgw < 256 and len(a0.images) == 1:
                anomalies["note"].append("%s: page bit used with a %d-px-wide image (CLUT rows %d)" % (n, imgw, a0.clut_h))
            if imgw >= 256 and not pages:
                anomalies["note"].append("%s: 256-px image but no page-1 faces" % n)
            for c in m.constructs:
                for f in c.faces:
                    for u, v in (f.uv or []):
                        if v >= imgh * len(a0.images) and len(a0.images) == 1:
                            raise FormatError("UV v=%d beyond image height %d" % (v, imgh))
        # external anim pack compatibility
        base = n.rsplit("/", 1)[0]
        prd = base + "/at0.prd"
        if prd in names:
            for k, packs in parse_prd_anims(arc.read(prd)).items():
                for a in packs:
                    if len(a.flags) != len(m.joints):
                        raise FormatError("external anim J=%d != model J=%d" % (len(a.flags), len(m.joints)))
        return m

    for cls in ("battle_player_obj", "battle_monster_obj", "field_obj"):
        if only and only != cls:
            continue
        chk(cls, model_check)
    if not only or only == "atim":
        chk("atim_ltd", lambda n: parse_atim_file(arc.read(n)))
        chk("atim_ltc", lambda n: parse_atim_file(arc.read(n)))
    if not only or only == "weapon":
        chk("weapon_kmd", lambda n: parse_mesh(arc.read(n), 0, len(arc.read(n))))
        chk("weapon_bin", lambda n: parse_weapon_bin(arc.read(n)))
    if not only or only == "field_ex_bin":
        def ex(n):
            an, bl = parse_ex_bin(arc.read(n))
            stem = n.rsplit("/", 1)[1][:-4]
            cand = [stem, stem.rstrip("0123456789")]
            for c_ in cand:
                mp = "fieldobj/player/%s/%s.obj" % (c_, c_)
                if mp in names:
                    j = len(parse_mdl(arc.read(mp)).joints)
                    if j != len(an[0].flags):
                        raise FormatError("ex J=%d != model %s J=%d" % (len(an[0].flags), mp, j))
                    return
            anomalies["note"].append("%s: no matching field model (J=%d)" % (n, len(an[0].flags)))
        chk("field_ex_bin", ex)
    if not only or only == "anim_prd":
        chk("anim_prd", lambda n: parse_prd_anims(arc.read(n)))
    if not only or only == "field_tim":
        chk("field_tim", lambda n: parse_tim(arc.read(n)))
    print("%-20s %6s %6s" % ("class", "ok", "total"))
    for cls, ok, tot in table:
        print("%-20s %6d %6d%s" % (cls, ok, tot, "" if ok == tot else "   <-- FAILURES"))
    for k, v in anomalies.items():
        print("\n%s (%d):" % (k, len(v)))
        for x in v[:40]:
            print("  ", x)
    return all(ok == tot for _, ok, tot in table)


def main(argv):
    if len(argv) < 2 or argv[1] not in ("info", "export", "validate"):
        print(__doc__)
        return 2
    arc = Archive()
    if argv[1] == "info":
        info(arc, argv[2])
    elif argv[1] == "export":
        export_model(arc, argv[2], argv[3])
        print("exported", argv[2], "->", argv[3])
    else:
        only = argv[3] if len(argv) > 3 and argv[2] == "--class" else None
        return 0 if validate(arc, only) else 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
