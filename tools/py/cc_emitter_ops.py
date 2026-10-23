"""cc_emitter_ops - operand histograms + range checks for the extended emitter ops (read-only).

Run:  python -I tools/py/cc_emitter_ops.py [--game DIR] [--quiet]

Walks every type-0x0A emitter of battle/effects/*.prd, map/mapbin/*.prd and effect/particle.prd, decodes the ops
0x34-0x37, 0x38-0x3F, 0x40-0x46, 0x48, 0x49 under the interpretation of the spec, prints per-op histograms of the
operand values and counts "anomalies" = operand values or stream contexts outside that interpretation.
Exit status 1 if any anomaly.  Standard library + cc_container/cc_prd/cc_tables.
"""
import argparse
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cc_container as cont
import cc_prd as prd
import cc_tables as tab

# ext-flag word (obj+0x94 = op 0x35 operand, u32 at cmd+4) bits the renderers/stepper read
EXT_KNOWN = 0x1FF | 0xE000 | 0x70000 | 0x180000 | 0x200000
MESH_TYPES = (0x02, 0x0E)      # name of a type-0x0E registry node = a type-0x02 drp entry


def u16(b, o): return struct.unpack_from("<H", b, o)[0]
def s16(b, o): return struct.unpack_from("<h", b, o)[0]
def u32(b, o): return struct.unpack_from("<I", b, o)[0]


def parse_curve(d):
    """type 0x18 entry.  Returns (nhead, nkeys, keys) or raises."""
    if len(d) < 0x34:
        raise ValueError("short")
    n = u32(d, 0x30)
    base = 0x34 + 8 * n
    if base + 0x14 > len(d):
        raise ValueError("head table overruns")
    m = u32(d, base)
    if base + 0x14 + 8 * m > len(d):
        raise ValueError("keys overrun")
    keys = [struct.unpack_from("<3hH", d, base + 0x14 + 8 * i) for i in range(m)]
    return n, m, keys


class Stats:
    def __init__(self, quiet):
        self.h = collections.defaultdict(collections.Counter)    # "op.field" -> Counter(value)
        self.n = collections.Counter()                           # op -> uses
        self.n_by = collections.Counter()                        # (op, src) -> uses
        self.anom = collections.Counter()
        self.ex = collections.defaultdict(list)
        self.quiet = quiet

    def add(self, key, v): self.h[key][v] += 1

    def bad(self, key, where, msg):
        self.anom[key] += 1
        if len(self.ex[key]) < 3:
            self.ex[key].append("%s: %s" % (where, msg))


def collect(g):
    srcs = []
    for nm in sorted(g.names("battle/effects/")) + sorted(g.names("map/mapbin/")) + ["effect/particle.prd"]:
        if nm.endswith(".prd"):
            srcs.append(("battle" if nm.startswith("battle") else "room", nm))
    return srcs


def run(g, st):
    lib = {}
    try:
        for e in cont.parse_drp(g.read("effect/particle.prd")):
            lib[(e.type, e.name)] = e
    except KeyError:
        pass
    total = 0
    for kind, nm in collect(g):
        es = cont.parse_drp(g.read(nm))
        reg = dict(lib)
        for e in es:
            reg[(e.type, e.name)] = e
        for e in es:
            if e.type != 0x0A:
                continue
            total += 1
            where = "%s/%s" % (nm, e.name)
            hd, cmds = prd.parse_effect(e.data)
            check_emitter(st, kind, where, hd, cmds, e.data, reg)
    return total


def check_emitter(st, kind, where, hd, cmds, raw, reg):
    big = bool(hd["flags"] & 0x202)           # bit1 (0xD8 object) or bit9 (0xFC object)
    huge = bool(hd["flags"] & 0x200)
    opsseq = [c.op for c in cmds]
    st.add("hdr.flags_bits1_9", (hd["flags"] >> 1 & 1, hd["flags"] >> 9 & 1))
    sprite = None                              # (type, name, blend at that time)
    flags06 = 0
    life = 0
    ext = None
    for c in cmds:
        op, p = c.op, c.off
        if op == 0x06:
            flags06 = u32(raw, p + 4)
            if flags06 >> 29 & 1 and not big: st.bad("06.bit29", where, "flag bit29 on a 0x94-byte object: draw reads obj+0x94..")
        elif op == 0x0F:
            life = u16(raw, p + 2)
        elif op == 0x05:
            sprite = (s16(raw, p + 12), raw[p + 8:p + 12].split(b"\0")[0].decode("latin1"), flags06 & 7)
            st.add("05.type_blend>=5", (sprite[0], sprite[2]) if sprite[2] >= 5 else None)
            if sprite[2] >= 5 and sprite[0] not in (3, 0xE, 0x11, 0x16):
                st.bad("05.blend57_type", where, "blend %d with sprite type %d" % (sprite[2], sprite[0]))
        if op not in (0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44,
                      0x45, 0x46, 0x48, 0x49):
            continue
        st.n[op] += 1
        st.n_by[(op, kind)] += 1
        if op == 0x34:
            k = u16(raw, p + 2); st.add("34.k", k)
        elif op == 0x35:
            v = u32(raw, p + 4); ext = v; st.add("35.ext", v)
            st.add("35.on_big_object", big)           # on a 0x94-byte object the engine skips op35 entirely
            if v & ~EXT_KNOWN: st.bad("35", where, "unknown ext bits 0x%X" % (v & ~EXT_KNOWN))
            st.add("35.bit0_with_op36", (bool(v & 1), 0x36 in opsseq))      # info: bits 0/1 are cleared by the engine, op36/37 set them
            st.add("35.bit1_with_op37", (bool(v & 2), 0x37 in opsseq))
            st.add("35.cmd+2", u16(raw, p + 2))
            if u16(raw, p + 2) != 0: st.bad("35", where, "cmd+2 nonzero")
        elif op == 0x36:
            q = struct.unpack_from("<4B2h2h", raw, p + 2)
            offx, offy = raw[p + 2], raw[p + 4]
            wx, wy, r, s = s16(raw, p + 6), s16(raw, p + 8), s16(raw, p + 10), s16(raw, p + 12)
            st.add("36.offx", offx); st.add("36.offy", offy); st.add("36.win_w", wx); st.add("36.win_h", wy)
            st.add("36.rate_u", r); st.add("36.rate_v", s); st.add("36.pad(+3,+5)", (raw[p + 3], raw[p + 5]))
            if not big: st.bad("36", where, "non-big object")
            for nme, val in (("win_w", wx), ("win_h", wy)):       # GP0 E2 texture window: 0 = no window, else power of two 8..256
                if val < 0 or val > 256 or (val and (val < 8 or val & (val - 1))): st.bad("36", where, "%s %d not 0 or power of two 8..256" % (nme, val))
            if offx & 7 or offy & 7: st.bad("36", where, "window offset not multiple of 8")
            if ext is None and not (flags06 >> 29 & 1): st.bad("36", where, "no op35 / flag bit29 before: orbit never steps")
        elif op == 0x37:
            mode, arg = u16(raw, p + 2), s16(raw, p + 4)
            name = raw[p + 12:p + 16].split(b"\0")[0].decode("latin1")
            st.add("37.mode", mode); st.add("37.rate", arg)
            if not big: st.bad("37", where, "non-big object")
            st.add("37.mode&7", mode & 7)           # engine uses (mode & 7) << 13; values 5..7 and the high garbage bits fall to linear
            if u32(raw, p + 8): st.bad("37", where, "cache field nonzero in file")
            if u32(raw, p + 4) >> 16 or False: pass
            if u16(raw, p + 6): st.bad("37", where, "+6 nonzero")
            tgt = [t for t in MESH_TYPES if (t, name) in reg]
            st.add("37.target_resolved", bool(tgt))            # unresolved -> cache 0 -> flag bit1 not set -> op inert
            st.add("37.sprite_is_mesh14", bool(sprite and sprite[0] == 14))
            if ext is None: st.bad("37.ext", where, "no op35 before (draw routine only reads morph when bit29 set)")
        elif op == 0x3D:
            mode = u16(raw, p + 2); name = raw[p + 8:p + 12].split(b"\0")[0].decode("latin1")
            st.add("3D.mode", mode)
            st.add("3D.mode&7", mode & 7)
            if u32(raw, p + 4): st.bad("3D", where, "cache nonzero")
            if (0x18, name) not in reg: st.bad("3D.name", where, "curve '%s' missing" % name)
            else:
                try: n_, m_, ks = parse_curve(reg[(0x18, name)].data)
                except ValueError as ex: st.bad("3D.curve", where, str(ex))
                else:
                    if any(k[3] > 0x1000 for k in ks) or any(a[3] > b[3] for a, b in zip(ks, ks[1:])): st.bad("3D.curve", where, "key times not monotone within 0..0x1000")
            if hd["duration"] == 0: st.bad("3D.dur", where, "header duration 0: curve never sampled")
            st.add("3D.pad(+12..19)", raw[p + 12:p + 20] == b"\0" * 8)
        elif op == 0x3E:
            mode = u16(raw, p + 2); name = raw[p + 8:p + 12].split(b"\0")[0].decode("latin1")
            st.add("3E.mode", mode); st.add("3E.huge", huge)
            st.add("3E.mode_s16", s16(raw, p + 2))
            if u32(raw, p + 4): st.bad("3E", where, "cache nonzero")
            if (0x18, name) not in reg: st.bad("3E.name", where, "curve '%s' missing" % name)
            else:
                try: n_, m_, ks = parse_curve(reg[(0x18, name)].data)
                except ValueError as ex: st.bad("3E.curve", where, str(ex))
                else:
                    st.add("3E.curve_keys", m_)
                    if any(k[3] > 0x1000 for k in ks) or any(a[3] > b[3] for a, b in zip(ks, ks[1:])): st.bad("3E.curve", where, "key times not monotone within 0..0x1000")
            if not huge: st.bad("3E.size", where, "writes obj+0xD0..0xFC but header lacks bit9 (0xFC object)")
            if life == 0: st.bad("3E.life", where, "no op0F lifetime before 3E (t=(life0-life)/life0)")
            if ext is None and not (flags06 >> 29 & 1): st.bad("3E.ext", where, "no op35/bit29: per-tick update never runs")
            st.add("3E.pad(+12..23)", raw[p + 12:p + 24] == b"\0" * 12)
        elif op in (0x3F, 0x40):
            st.add("%02X.sprite_type" % op, sprite[0] if sprite else None)
            if sprite is None: st.bad("%02X" % op, where, "no op05 before: obj+0x38 node null, op does nothing")
        elif op == 0x41:
            a = struct.unpack_from("<3H3h", raw, p + 2)
            st.add("41.base", a[:3]); st.add("41.spread", a[3:])
            if any(x == -1 for x in a[3:]): st.bad("41", where, "spread -1: rand() % 0")    # rand()%(s+1)-s/2, angles wrap mod 4096
        elif op == 0x43 or op == 0x44:
            xyz = struct.unpack_from("<3h", raw, p + 2)
            st.add("%02X.xyz" % op, xyz)
            need = 19 if op == 0x43 else 20
            if not big: st.bad("%02X.size" % op, where, "writes obj+0xB8..0xCF but object is 0x94 bytes")
            if ext is None or not (ext >> need & 1): st.bad("%02X" % op, where, "ext bit%d not enabled by a preceding op35" % need)
        elif op == 0x45:
            name = raw[p + 8:p + 12].split(b"\0")[0].decode("latin1")
            st.add("45.child_resolved", (0x0A, name) in reg)       # unresolved -> child handle stays 0 -> no child
        elif op == 0x48:
            st.add("48.ptr", u32(raw, p + 4))
            st.add("48.rest", raw[p + 2:p + 4].hex() + "/" + raw[p + 8:p + 12].hex())
            if u32(raw, p + 4): st.bad("48", where, "nonzero pointer in file")
        elif op == 0x49:
            a, b, cb, cl = raw[p + 2], raw[p + 3], raw[p + 4], raw[p + 5]
            d, n = u16(raw, p + 6), s16(raw, p + 8)
            st.add("49.show,bit200,bit100,clone", (a, b, cb, cl)); st.add("49.anim_speed", d); st.add("49.n", n)
            if sprite is None or sprite[0] != 0x14: st.bad("49.sprite", where, "sprite type %s is not 0x14" % (sprite and sprite[0]))
            if max(a, b, cb, cl) > 1: st.bad("49", where, "flag byte > 1")
            if n < 0 or n > 0x1FFF: st.bad("49", where, "n %d outside 0..0x1FFF (the engine masks with &0x1FFF)" % n)
            if not big: st.bad("49.size", where, "writes obj+0xD4 but object is 0x94 bytes")
            if ext is None or not (ext >> 21 & 1): st.bad("49.ext", where, "ext bit21 (animate model) not set by op35")
        elif op == 0x38:
            if life == 0: st.bad("38", where, "lifetime 0 before op38: nothing integrated")
        elif op == 0x39:
            f = u16(raw, p + 6)
            st.add("39.abcd", tuple(raw[p + 2:p + 6])); st.add("39.f", f)
            if f & ~7: st.bad("39", where, "flags 0x%X outside bits 0..2 (only 1,2 are read)" % f)
        elif op == 0x3A:
            st.add("3A", (u16(raw, p + 2), u16(raw, p + 4)))
        elif op == 0x42:
            st.add("42.has_op39", 0x39 in opsseq)       # without op39 the engine falls back to op46 behaviour
        elif op in (0x3B, 0x46):
            pass


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--game", default=None)
    ap.add_argument("--quiet", action="store_true", help="only totals + anomalies")
    ap.add_argument("--top", type=int, default=8)
    a = ap.parse_args(argv)
    g = tab.Game(a.game)
    st = Stats(a.quiet)
    tot = run(g, st)
    print("emitters scanned: %d" % tot)
    print("op uses (battle/room): " + ", ".join("%02X:%d/%d" % (op, st.n_by[(op, "battle")], st.n_by[(op, "room")])
                                              for op in sorted(st.n)))
    if not a.quiet:
        for k in sorted(st.h):
            c = st.h[k]
            print("%-22s distinct=%-5d top: %s" % (k, len(c), ", ".join("%s x%d" % (v, n) for v, n in c.most_common(a.top))))
    tot_bad = sum(st.anom.values())
    print("anomalies: %d" % tot_bad)
    for k in sorted(st.anom):
        print("  %-12s %5d  e.g. %s" % (k, st.anom[k], " | ".join(st.ex[k])))
    return 1 if tot_bad else 0


if __name__ == "__main__":
    sys.exit(main())
