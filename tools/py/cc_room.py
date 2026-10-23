"""Chrono Cross (PS1 / Radical Dreamers Edition) room decoder.

Usage (always run with -I):
  python -I cc_room.py --game <RDE dir> info <room>
  python -I cc_room.py --game <RDE dir> render <room> <outdir>
  python -I cc_room.py --game <RDE dir> walkmesh <room> <out.obj>
  python -I cc_room.py --game <RDE dir> validate [--limit N]

Every room is three files
in data/cdrom.dat:  map/mapbin/<room>.bin (cpt), .ctd (cpt of TIMs), .prd (drp).
"""
import argparse
import os
import struct
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cc_container as cont
import cc_lzss
import cc_tim

MAPDIR = "map/mapbin/"
CLUT_VRAM_BYTE_BASE = 0x70000            # bin subfile 1 is copied here (VRAM row 224, x 0)
TILE = 16

# bin (script.cpt) subfile indices
SEC_CAMERA, SEC_CLUT, SEC_STUB, SEC_SCRIPT, SEC_TRIS, SEC_VERTS, SEC_CONST, \
    SEC_LAYERS, SEC_DIALOG, SEC_SND1, SEC_SND0, SEC_MODEL0 = range(12)


class GameFiles:
    """Read-only access to data/cdrom.dat (plain ZIP)."""

    def __init__(self, game_dir):
        self.zip = zipfile.ZipFile(os.path.join(game_dir, "data", "cdrom.dat"))
        self.names = set(self.zip.namelist())

    def read(self, name):
        return self.zip.read(name)

    def has(self, name):
        return name in self.names

    def rooms(self):
        return sorted(n[len(MAPDIR):-4] for n in self.names
                      if n.startswith(MAPDIR) and n.endswith(".bin"))


# ----------------------------------------------------------------- data classes
class Camera:
    """Subfile 0."""

    def __init__(self, s):
        self.raw = s
        self.matrix = struct.unpack_from("<9h", s, 0)          # 4.12 rotation, row-major
        self.trans = struct.unpack_from("<3h", s, 18)          # translation
        self.proj_h = struct.unpack_from("<h", s, 24)[0]       # GTE H (projection distance)
        self.x_max, self.x_min, self.y_max, self.y_min = struct.unpack_from("<4h", s, 26)
        self.unk34 = struct.unpack_from("<h", s, 34)[0]
        self.n_groups = struct.unpack_from("<I", s, 36)[0]
        self.group_scale = [struct.unpack_from("<I", s, 40 + 4 * i)[0]
                            for i in range(min(self.n_groups, (len(s) - 40) // 4))]


class Tile:
    __slots__ = ("x", "y", "u", "v", "clut", "group", "flags", "z", "index", "layer")

    @property
    def semi(self):
        return bool(self.flags & 0x80)

    @property
    def abr(self):
        return (self.flags >> 3) & 3


class Layer:
    def __init__(self, index, tiles):
        self.index, self.tiles = index, tiles


class Walkmesh:
    def __init__(self, tris, verts):
        self.tris = tris        # list of (v0, v1, v2, adj0, adj1, adj2, attr)
        self.verts = verts      # list of (x, y, z, w)


class ModelHeader:
    def __init__(self, s):
        self.eof, self.img_x, self.img_y, self.clut_x, self.clut_y = struct.unpack_from("<IHHHH", s, 0)
        self.size = len(s)


class Room:
    def __init__(self, name, bin_sections, ctd_sections, prd):
        self.name = name
        self.sec = bin_sections
        self.ctd = ctd_sections
        self.prd = prd
        self.camera = Camera(bin_sections[SEC_CAMERA])
        self.clut_rows = len(bin_sections[SEC_CLUT]) // 512
        self.layers = parse_layers(cc_lzss.decode(bin_sections[SEC_LAYERS]))
        self.walkmesh = parse_walkmesh(bin_sections[SEC_TRIS], bin_sections[SEC_VERTS])
        self.models = [ModelHeader(s) for s in bin_sections[SEC_MODEL0:]]
        # .ctd entries are TIMs, either raw or wrapped in sszl
        self.ctd_packed = [cc_lzss.is_sszl(s) for s in ctd_sections]
        self.tims = [cc_tim.parse_tim(cc_lzss.decode(s) if cc_lzss.is_sszl(s) else s) for s in ctd_sections]
        self._vram = None

    # script / dialogue are kept as opaque decompressed bytes
    @property
    def script(self):
        return cc_lzss.decode(self.sec[SEC_SCRIPT])

    @property
    def dialogue(self):
        return cc_lzss.decode(self.sec[SEC_DIALOG])

    @property
    def vram(self):
        """VRAM as the game builds it: all .ctd TIMs, then bin subfile 1 CLUTs."""
        if self._vram is None:
            v = cc_tim.Vram()
            for t in self.tims:
                v.blit_tim(t)
            c = self.sec[SEC_CLUT]
            for k in range(0, len(c), 512):          # palette k = VRAM row 224+k, x 0 (256-wide LoadImage rect)
                v.write_bytes(CLUT_VRAM_BYTE_BASE + (k // 512) * 2048, c[k:k + 512])
            self._vram = v
        return self._vram


def parse_layers(d):
    """Decoded subfile 7 -> list[Layer].  Table = u32 N, u32 start[N+1]; the last
    entry is the index of the LAST tile (not a count)."""
    n = struct.unpack_from("<I", d, 0)[0]
    if n == 0 or n > 64:
        raise cont.FormatError("layer count %d" % n)
    start = struct.unpack_from("<%dI" % (n + 1), d, 4)
    base = 4 + 4 * (n + 1)
    ntile = start[n] + 1
    if base + 12 * ntile > len(d):
        raise cont.FormatError("tile data truncated")
    if any(start[i] > start[i + 1] for i in range(n)):
        raise cont.FormatError("layer table not monotonic")
    layers = []
    for li in range(n):
        end = start[li + 1] if li + 1 < n else ntile      # last layer runs to the last tile
        tiles = []
        for ti in range(start[li], end):
            x, y, u, v, c, g, f, z = struct.unpack_from("<hhHBBBBH", d, base + 12 * ti)
            t = Tile()
            t.x, t.y, t.u, t.v, t.clut, t.group, t.flags, t.z = x, y, u, v, c, g, f, z
            t.index, t.layer = ti, li
            tiles.append(t)
        layers.append(Layer(li, tiles))
    return layers


def parse_walkmesh(tri_sec, vert_sec):
    nt = struct.unpack_from("<I", tri_sec, 0)[0]
    if len(tri_sec) not in (4 + 14 * nt, 4 + 14 * nt + 2):    # +2 = pad to 4 bytes
        raise cont.FormatError("triangle section size %d for %d tris" % (len(tri_sec), nt))
    bl = struct.unpack_from("<I", vert_sec, 0)[0]
    if bl % 8 or 4 + bl != len(vert_sec):
        raise cont.FormatError("vertex section size mismatch")
    tris = [struct.unpack_from("<HHHhhhh", tri_sec, 4 + 14 * i) for i in range(nt)]
    verts = [struct.unpack_from("<hhhh", vert_sec, 4 + 8 * i) for i in range(bl // 8)]
    return Walkmesh(tris, verts)


def load_room(gf, name):
    sections = cont.parse_cpt(gf.read(MAPDIR + name + ".bin"))
    ctd = cont.parse_cpt(gf.read(MAPDIR + name + ".ctd"))
    prd = None
    if gf.has(MAPDIR + name + ".prd"):
        prd = cont.parse_drp(gf.read(MAPDIR + name + ".prd"))
    return Room(name, sections, ctd, prd)


# ----------------------------------------------------------------- rendering
def tile_pixels(room, t):
    """Decode one 16x16 tile to a list of 256 15-bit colours (0 = transparent).
    Texels: 8bpp, halfword column t.u, row t.v + 256; CLUT = VRAM row 224 + clut/8."""
    vr = room.vram
    clut = vr.clut256(0, 224 + (t.clut >> 3))
    out = []
    for r in range(TILE):
        idx = vr.row8(t.u, 256 + t.v + r, TILE)
        out.extend(clut[i] for i in idx)
    return out


def bbox(tiles):
    return (min(t.x for t in tiles), min(t.y for t in tiles),
            max(t.x for t in tiles) + TILE, max(t.y for t in tiles) + TILE)


def blend(back, c, abr):
    """Semi-transparency on 8-bit channels (PS1 ABR modes)."""
    if abr == 0:
        return [(b + f) // 2 for b, f in zip(back, c)]
    if abr == 1:
        return [min(255, b + f) for b, f in zip(back, c)]
    if abr == 2:
        return [max(0, b - f) for b, f in zip(back, c)]
    return [min(255, b + f // 4) for b, f in zip(back, c)]


def draw_tiles(room, tiles, box, canvas, composite):
    """Draw `tiles` into an RGBA bytearray sized from `box`.  composite=True
    applies semi-transparency against what is already there."""
    x0, y0, x1, _y1 = box
    w = x1 - x0
    for t in tiles:
        px = tile_pixels(room, t)
        for r in range(TILE):
            yy = t.y + r - y0
            for c in range(TILE):
                col = px[r * TILE + c]
                if col == 0:
                    continue
                o = (yy * w + (t.x + c - x0)) * 4
                rgba = cc_tim.bgr555_to_rgba(col)
                if composite and t.semi and (col & 0x8000):
                    canvas[o:o + 3] = bytes(blend(list(canvas[o:o + 3]), rgba[:3], t.abr))
                    canvas[o + 3] = 255
                else:
                    canvas[o:o + 4] = bytes(rgba)


def render_room(room, outdir):
    os.makedirs(outdir, exist_ok=True)
    alltiles = [t for L in room.layers for t in L.tiles]
    box = bbox(alltiles)
    w, h = box[2] - box[0], box[3] - box[1]
    for L in room.layers:
        if not L.tiles:
            continue
        cv = bytearray(w * h * 4)
        draw_tiles(room, L.tiles, box, cv, composite=False)
        cc_tim.write_png(os.path.join(outdir, "%s_layer%02d.png" % (room.name, L.index)), w, h, cv)
    # composite: far (large z) first; stable by (layer, index)
    order = sorted(alltiles, key=lambda t: (-t.z, t.layer, t.index))
    cv = bytearray(w * h * 4)
    draw_tiles(room, order, box, cv, composite=True)
    cc_tim.write_png(os.path.join(outdir, "%s_composite.png" % room.name), w, h, cv)
    return box


def write_obj(room, path):
    wm = room.walkmesh
    with open(path, "w") as f:
        f.write("# %s walk mesh: raw PS1 coordinates (Y points down)\n" % room.name)
        for x, y, z, _w in wm.verts:
            f.write("v %d %d %d\n" % (x, y, z))
        for t in wm.tris:
            f.write("f %d %d %d\n" % (t[0] + 1, t[1] + 1, t[2] + 1))


# ----------------------------------------------------------------- validation
def check_room(gf, name):
    """Return (anomalies, stats).  Never raises."""
    an, st = [], {}
    try:
        room = load_room(gf, name)
    except Exception as e:                      # container / parse failure
        return ["LOAD: %s" % e], st
    st["layers"] = len(room.layers)
    st["tiles"] = sum(len(L.tiles) for L in room.layers)
    cam = room.camera
    if len(room.sec[SEC_CAMERA]) != 40 + 4 * cam.n_groups:
        an.append("CAM: size %d != 40+4*%d" % (len(room.sec[SEC_CAMERA]), cam.n_groups))
    groups = {t.group for L in room.layers for t in L.tiles}
    if groups and cam.n_groups != max(groups) + 1:
        an.append("CAM: n_groups %d != max tile group+1 %d" % (cam.n_groups, max(groups) + 1))
    for i in (SEC_STUB, SEC_SCRIPT, SEC_DIALOG, SEC_LAYERS):
        s = room.sec[i]
        if not cc_lzss.is_sszl(s):
            an.append("SSZL: subfile %d lacks magic" % i)
        elif len(cc_lzss.decode(s)) != cc_lzss.declared_length(s):
            an.append("SSZL: subfile %d short decode" % i)
    if len(room.sec[SEC_CLUT]) % 512:
        an.append("CLUT: size not multiple of 512")
    if len(room.sec[SEC_CONST]) not in (324, 332):
        an.append("CONST: subfile 6 size %d" % len(room.sec[SEC_CONST]))
    # tiles: flags, clut index, source coverage by .ctd TIMs
    cover = bytearray(cc_tim.VRAM_W * cc_tim.VRAM_H)
    for t in room.tims:
        x, y, w, h, _ = t.image
        for r in range(h):
            cover[(y + r) * cc_tim.VRAM_W + x:(y + r) * cc_tim.VRAM_W + x + w] = b"\1" * w
    bad_flags = bad_clut = bad_src = bad_align = 0
    for L in room.layers:
        for t in L.tiles:
            if (t.flags & 0x60) != 0x20:
                bad_flags += 1
            if t.clut & 7 or (t.clut >> 3) >= room.clut_rows:
                bad_clut += 1
            if t.u % 8 or t.v % 16:
                bad_align += 1
            ok = t.u + 8 <= cc_tim.VRAM_W
            if ok:
                for r in (0, 15):
                    row = (256 + t.v + r) * cc_tim.VRAM_W + t.u
                    if not (cover[row] and cover[row + 7]):
                        ok = False
            if not ok:
                bad_src += 1
    for label, n in (("tile flags (b9&0x60!=0x20)", bad_flags), ("tile clut out of range", bad_clut),
                     ("tile u/v not 8/16 aligned", bad_align), ("tile source not covered by a .ctd TIM", bad_src)):
        if n:
            an.append("TILE: %d x %s" % (n, label))
    if any(not L.tiles for L in room.layers):
        an.append("LAYER: empty layer(s)")
    tiles = [t for L in room.layers for t in L.tiles]
    st["bbox_match"] = (bbox(tiles) == (-cam.x_max, -cam.y_max, 320 - cam.x_min, 240 - cam.y_min))
    # walk mesh
    wm = room.walkmesh
    nv, nt = len(wm.verts), len(wm.tris)
    for i, t in enumerate(wm.tris):
        if max(t[:3]) >= nv:
            an.append("WALK: tri %d vertex index out of range" % i)
        if len(set(t[:3])) < 3:
            an.append("WALK: tri %d degenerate %s" % (i, t[:3]))
        for k in range(3):
            j = t[3 + k]
            if j < 0:
                continue
            if j >= nt:
                an.append("WALK: tri %d adj out of range" % i)
                continue
            if not ({t[k], t[(k + 1) % 3]} <= set(wm.tris[j][:3])):
                an.append("WALK: tri %d adj%d does not share edge" % (i, k))
            elif i not in wm.tris[j][3:6]:
                an.append("WALK: tri %d adj%d not reciprocal" % (i, k))
    if any(v[3] for v in wm.verts):
        an.append("WALK: vertex w != 0")
    # every model subfile header must name a CLUT-carrying TIM of the .ctd (image xy + CLUT xy)
    clut_keys = {(t.image[0], t.image[1], t.clut[0], t.clut[1]) for t in room.tims if t.has_clut}
    for k, m in enumerate(room.models):
        if m.eof != m.size - 12:
            an.append("MODEL %d: eof field %d != size-12 %d" % (k, m.eof, m.size - 12))
        if (m.img_x, m.img_y, m.clut_x, m.clut_y) not in clut_keys:
            an.append("MODEL %d: header (%d,%d,%d,%d) matches no CLUT TIM in .ctd" % (
                k, m.img_x, m.img_y, m.clut_x, m.clut_y))
    st["extra_clut_tims"] = len(clut_keys) - len({(m.img_x, m.img_y, m.clut_x, m.clut_y) for m in room.models})
    bg = [t for t in room.tims if not t.has_clut]
    if bg and bg[0].image[:2] != (0, 256):
        an.append("CTD: first background TIM at %s, expected (0,256)" % (bg[0].image[:2],))
    if room.prd is not None:
        st["prd_types"] = sorted({e.type for e in room.prd})
        st["prd_entries"] = len(room.prd)
        st["prd_len_mismatch"] = sum(1 for e in room.prd if e.length != len(e.data))
    return an, st


def validate(gf, limit=None):
    rooms = gf.rooms()
    if limit:
        rooms = rooms[:limit]
    total_files = 0
    fails = []
    types = {}
    bbox_ok = extra = ent = mism = 0
    for r in rooms:
        total_files += 1 + gf.has(MAPDIR + r + ".ctd") + gf.has(MAPDIR + r + ".prd")
        an, st = check_room(gf, r)
        bbox_ok += bool(st.get("bbox_match"))
        extra += st.get("extra_clut_tims", 0) > 0
        ent += st.get("prd_entries", 0)
        mism += st.get("prd_len_mismatch", 0)
        for ty in st.get("prd_types", ()):
            types[ty] = types.get(ty, 0) + 1
        if an:
            fails.append((r, an))
    print("rooms: %d  files: %d  clean: %d  with anomalies: %d  bbox==viewport+pan: %d" %
          (len(rooms), total_files, len(rooms) - len(fails), len(fails), bbox_ok))
    print("rooms whose .ctd has CLUT TIMs not referenced by a model subfile: %d" % extra)
    print("prd entries: %d, header length != payload span: %d" % (ent, mism))
    print("prd entry types seen (rooms containing): %s" % ", ".join(
        "%02X:%s=%d" % (k, cont.DRP_TYPES.get(k, "?"), v) for k, v in sorted(types.items())))
    print("%-14s %s" % ("room", "anomaly"))
    for r, an in fails:
        for a in an[:6]:
            print("%-14s %s" % (r, a))
        if len(an) > 6:
            print("%-14s ... and %d more" % (r, len(an) - 6))
    return fails


# ----------------------------------------------------------------- CLI
def cmd_info(gf, name):
    room = load_room(gf, name)
    cam = room.camera
    print("room %s: %d bin subfiles, %d ctd TIMs, prd=%s" % (
        name, len(room.sec), len(room.tims), "none" if room.prd is None else "%d entries" % len(room.prd)))
    for i, s in enumerate(room.sec):
        tag = s[:4].decode("latin1") if s[:4] in (b"sszl", b"yos0", b"yos1") else ""
        print("  bin[%2d] %7d bytes %s" % (i, len(s), tag))
    print("camera: matrix=%s trans=%s H=%d pan x[%d..%d] y[%d..%d] group scales=%s" % (
        cam.matrix, cam.trans, cam.proj_h, cam.x_min, cam.x_max, cam.y_min, cam.y_max, cam.group_scale))
    print("cluts: %d rows" % room.clut_rows)
    for L in room.layers:
        if L.tiles:
            print("  layer %d: %4d tiles bbox=%s z=%s groups=%s flags=%s" % (
                L.index, len(L.tiles), bbox(L.tiles), sorted({t.z for t in L.tiles}),
                sorted({t.group for t in L.tiles}), ["%02X" % f for f in sorted({t.flags for t in L.tiles})]))
    print("walkmesh: %d tris, %d verts" % (len(room.walkmesh.tris), len(room.walkmesh.verts)))
    for i, t in enumerate(room.tims):
        print("  ctd[%2d] bpp=%d img=%s clut=%s" % (i, t.bpp, t.image[:4], t.clut[:4] if t.clut else None))
    for k, m in enumerate(room.models):
        print("  model %d: img=(%d,%d) clut=(%d,%d) size=%d" % (k, m.img_x, m.img_y, m.clut_x, m.clut_y, m.size))
    for e in room.prd or ():
        print("  prd[%2d] %-4s type=%02X (%s) data=%d" % (
            e.index, e.name, e.type, cont.DRP_TYPES.get(e.type, "?"), len(e.data)))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", required=True, help="RDE install dir (contains data/cdrom.dat)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("info"); p.add_argument("room")
    p = sub.add_parser("render"); p.add_argument("room"); p.add_argument("outdir")
    p = sub.add_parser("walkmesh"); p.add_argument("room"); p.add_argument("out")
    p = sub.add_parser("validate"); p.add_argument("--limit", type=int)
    a = ap.parse_args(argv)
    gf = GameFiles(a.game)
    if a.cmd == "info":
        cmd_info(gf, a.room)
    elif a.cmd == "render":
        print("box", render_room(load_room(gf, a.room), a.outdir))
    elif a.cmd == "walkmesh":
        write_obj(load_room(gf, a.room), a.out)
    else:
        return 1 if validate(gf, a.limit) else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
