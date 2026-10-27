"""cc_effects - battle element/tech/summon effect packs (RDE cdrom.dat), read-only.

Standard library + cc_container/cc_prd/cc_tables only.
Run with:  python -I tools/py/cc_effects.py [--game DIR] <command> [args]

  map [--all]       element id -> effect id -> (egfx .ctd, eins .prd, overlay .prg); --all lists every effect slot
  info <file>       a battle/effects member (e.g. white/33lez.prd, tech/3dan.ctd) or an effect id (number)
  validate          parse all battle/effects files, the effect overlays and the effect-slot table
  stats             distinct overlays/programs, op histograms, reproducibility classes
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

EFFDIR = "battle/effects/"
FID_GROUP = 11                 # SetDir(0xB): the effect group in cdrom.fid
SECTOR = 2048
GENERIC_OVERLAYS = ("stdtech0.prg", "stdtech1.prg", "stdmag0.prg", "magtest.prg")  # tiny, data-driven hosts

# ---- battle sequence (type 0x10) op table.  Framing: u16 w: op = w&0x1FF, len = (w>>9)&0x3F, bit15 = indirect
# jump, u16 wait-after at +2.  Names are ours; descriptions come from the handler bodies.  Ops >= 0x100 are overlay-private.
SEQ_OPS = {
    0x20: ("MARK", "executor: no-op marker/wait"),
    0x21: ("END", "executor: ends the sequence"),
    0x22: ("START_EMITTER", "start type-0x0A entry named at +12 (FUN_80040CE4; optional per-slot clone)"),
    0x23: ("CAMERA", "play type-0x0C camera record named at +12; run of 0x23 with wait 0 -> rand() pick (FUN_80032954)"),
    0x24: ("SCREEN_HOLD_ON", "FUN_800413D0(1)"),
    0x25: ("NOP25", "empty case"),
    0x26: ("FX_ALL_ON", "FUN_8002CB44/68/8C + CB20"),
    0x27: ("FX_ALL_OFF", "FUN_8002CA90/B4/FC + CAD8"),
    0x28: ("SET_TIMER24", "seq task +0x24 = arg (screen-shake/vibration timer)"),
    0x29: ("RAMP_LEVEL", "seq task level (+0x1E, x0x80) ramps to arg over N frames (feeds 'vib_l' / FUN_8002BAB8)"),
    0x2A: ("COLOR_MATRIX", "type-0x11 entry by name -> GTE colour matrix (SetColorMatrix)"),
    0x2B: ("FADE_IN", "FUN_8002E3CC(r,g,b,mode,frames)"),
    0x2C: ("FADE_OUT", "FUN_8002E288(r,g,b,mode,frames)"),
    0x2D: ("SCREEN_FLASH", "0x14-byte task FUN_8002C3FC: timed full-screen colour"),
    0x2E: ("TASK2E", "FUN_8003695C(a,b,frames)"),
    0x2F: ("CHILD_SEQ", "start type-0x10 entry named at +12 as a child task"),
    0x30: ("MODELS_A_SHOW", "FUN_8009CD9C(model,1) for all non-excluded objects"),
    0x31: ("MODELS_A_HIDE", "FUN_8009CD88(model,1)"),
    0x32: ("PC_SHOW_NONTARGET", "conditional show (FUN_8009CD9C)"),
    0x33: ("PC_HIDE_NONTARGET", "conditional hide (FUN_8009CD88)"),
    0x34: ("SHOW_FLAGGED", "conditional show, flag set"),
    0x35: ("HIDE_FLAGGED", "conditional hide, flag set"),
    0x36: ("FX_LAYER1_ON", "FUN_8002CB68"), 0x37: ("FX_LAYER2_ON", "FUN_8002CAB4"),
    0x38: ("FX_LAYER0_ON", "FUN_8002CB44"), 0x39: ("FX_LAYER0_OFF", "FUN_8002CA90"),
    0x3A: ("FX_ALL_ON_B", "FUN_8002CB20"), 0x3B: ("FX_ALL_OFF_B", "FUN_8002CAD8"),
    0x3C: ("FX_LAYER3_ON", "FUN_8002CB8C"), 0x3D: ("FX_LAYER3_OFF", "FUN_8002CAFC"),
    0x3E: ("FXSTATE_BEGIN", "DAT_800720D8=1 and reset the tint/light accumulators (DAT_800720E0..FC)"),
    0x3F: ("FXSTATE_END", "DAT_800720D8=0"),
    0x40: ("TINT_RAMP", "0x34-byte task FUN_8002C5A8: ramps the global tint/light vector over N frames (28-byte op)"),
    0x41: ("OVERLAY_END", "FUN_8002BC08: tear down the effect overlay"),
    0x42: ("WAIT_LOADED", "executor [C]: stall while FUN_80044D60() (CD streaming) is busy"),
    0x43: ("ALL_PC_LIGHT", "model brightness 0x80+arg for all party members (FUN_800A1CD0)"),
    0x44: ("ALL_ENEMY_LIGHT", "model brightness for all enemies"),
    0x45: ("PC_LIGHT", "model brightness for one party slot"),
    0x46: ("ENEMY_LIGHT", "model brightness for one enemy slot (+target offset)"),
    0x47: ("ENEMY_FN1", "battle.prg FUN_800B0660(one enemy)"),
    0x48: ("ALL_ENEMY_FN1", "same for all enemies"),
    0x49: ("MODELS_B_FN", "FUN_8009CD78(model) for objects (party list)"),
    0x4A: ("ENEMY_B_FN", "FUN_8009CD78 for enemies"),
    0x4B: ("PC_B_FN", "FUN_8009CD78 for party"),
    0x4C: ("SFX_BY_NAME", "type-0x16 AKAO sound named at +12 -> Sound_PlaySfxFromPointer(ptr, vol, pan, pitch) (24-byte op)"),
    0x4D: ("NEXT_TARGET_LOOP", "executor [C]: if the sequence runs per-target and another battle object exists, restart the loop body for the next object"),
    0x4E: ("LOOP_LABEL", "executor: sets the sequence restart pointer (bit15 = indirect)"),
    0x4F: ("PC_AMBIENT", "model ambient for one party slot"),
    0x50: ("WAIT_ACTOR_FLAG", "executor [C]: stalls while *(actor_obj0+0x108) != 0 (waits for the caster action to reach a point) [I direction]"),
    0x51: ("SFX_ID", "Sound_PlaySfx(id, ...)"),
    0x52: ("COUNTER_INC", "DAT_80071C84++"),
    0x53: ("FN_B388", "FUN_8002B388"),
    0x54: ("MUSIC_FADE", "Sound_FadeMasterVolumeByMusicId(0, frames, level)"),
    0x55: ("EMITTER_STOP", "FUN_80040734 when the +8 cache is set"),
    0x56: ("SET_RENDERFLAG", "DAT_80072100 = arg (or DAT_800720FF)"),
    0x57: ("ENEMY_AMBIENT0", "enemy model ambient, mode 0"),
    0x58: ("MODEL_COLOR_FADE", "16-byte task FUN_8002CD0C: fades one model's colour bytes (+0x2C..2E)"),
    0x59: ("MODEL_FN_CF54", "FUN_8009CF54(model, arg, 0x20)"),
    0x5A: ("EMITTER_KILL", "FUN_80041018"),
    0x5B: ("ENEMY_SHOW_A", "show all enemies (FUN_8009CD9C)"), 0x5C: ("ENEMY_HIDE_A", "FUN_8009CD88"),
    0x5D: ("PC_SHOW_A", "show all party"), 0x5E: ("PC_HIDE_A", "hide all party"),
    0x5F: ("BTL_FN_0964", "battle.prg FUN_800B0964 if DAT_800DE76C"), 0x60: ("BTL_FN_08D8", "battle.prg FUN_800B08D8"),
    0x61: ("ENEMY_HIDE_B", "FUN_8009CD88(model,0x100)"), 0x62: ("ENEMY_SHOW_B", "FUN_8009CD9C(model,0x100)"),
    0x63: ("PC_HIDE_B", "same for party"), 0x64: ("PC_SHOW_B", "same for party"),
    0x65: ("TASK_FOG_COLOR", "12-byte task FUN_8002C2D0 (rgb fade to DAT_800721DC)"),
    0x66: ("FN_1BBC_ALL", "FUN_80021BBC(0..2)"),
    0x67: ("YIELD", "executor [C]: end this frame (wait=1), continue at the next op"),
    0x68: ("OBJ_HIDE_C", "FUN_8009CD88 over the third object list"), 0x69: ("OBJ_SHOW_C", "FUN_8009CD9C over the third object list"),
    0x6A: ("LOAD_SUBPACK", "FUN_8002CCB0 task + CD load of file index 3 of the effect group"),
    0x6B: ("BTL_FN_06AC", "battle.prg FUN_800B06AC(first party object)"),
    0x6C: ("SCREEN_HOLD_OFF", "FUN_800413D0(0)"),
    0x6D: ("VAR_RAMP", "task FUN_8002CBB0: ramp _DAT_800DE75C to arg"),
    0x6E: ("VAR_RAMP_100", "ramp _DAT_800DE75C to 100"),
    0x6F: ("ENEMY_FN_C274", "FUN_8009C274(enemy,1)"), 0x70: ("ENEMY_FN_C3B4", "FUN_8009C3B4(enemy)"),
    0x71: ("ALL_ENEMY_FN_C274", ""), 0x72: ("ALL_ENEMY_FN_C3B4", ""),
    0x73: ("FLAG_D9", "DAT_800720D9=1"),
    0x74: ("MODEL_FLAG200_CLR", "enemy model +0x7E &= ~0x200"), 0x75: ("MODEL_FLAG200_SET", "enemy model +0x7E |= 0x200"),
    0x76: ("FACING_FLIP", "model +0xA6 = +-0x400 by sign of +0x84"),
    0x77: ("PC_FN_C274", ""), 0x78: ("PC_FN_C3B4", ""),
    0x79: ("SET_FF", "DAT_800720FF = arg"),
    0x7A: ("WAIT_COND", "executor [C]: FUN_8001AA8C(op) condition; stalls until true"),
    0x7B: ("BTL_FLAG", "DAT_800DE76C = 1"),
}
OVERLAY_OPS_MIN = 0x100        # ops >= 0x100 are not in the default handler: the effect overlay must interpret them


def seq_ops(b):
    """Generic type-0x10 walk: header 0x14 bytes, u16 op/len/indirect, u16 wait.  Returns [(off, op, len, wait, name)]."""
    out, p = [], 0x14
    while p + 4 <= len(b):
        w, wait = struct.unpack_from("<HH", b, p)
        op, ln = w & 0x1FF, (w >> 9) & 0x3F
        if ln < 4 or p + ln > len(b):
            raise prd.StreamError("sequence op 0x%X length %d at 0x%X" % (op, ln, p))
        name = b[p + 12:p + 16].split(b"\0")[0].decode("latin1") if op in (0x22, 0x23, 0x2F, 0x2A, 0x4C) and ln >= 16 else None
        out.append((p, op, ln, wait, name))
        p += ln
        if op == 0x21:
            return out
    raise prd.StreamError("sequence has no END")


# ---------------------------------------------------------------- slot table (cdrom.fnd / cdrom2.fnd)
DISC2_ONLY = set()             # filled by eff_slots: eids whose files exist only in the disc-2 index
IRREGULAR = (1, 15, 16, 28)    # slots that hold bttim / syoukan .mtc/.dat / battle/bg data instead of a triple


def load_fnd(g, name="cdrom.fnd"):
    raw = g.read(name)
    bs = chr(92)
    pre = "c:" + bs + "work" + bs + "cdrom" + bs
    out = []
    for i in range(len(raw) // 64):
        s = raw[i * 64:(i + 1) * 64].split(b"\0")[0].decode("latin1").lower()
        out.append(s.replace(pre, "").replace(bs, "/"))
    return out


def fid_base(g):
    return struct.unpack("<17H", g.read("cdrom.fid"))[FID_GROUP]


def slot(fnd, eid):
    """Effect id -> (ctd, prd, prg) names.  Loader: files fid[11]+3*(eid-1)+{1,2,3} = egfx (loaded to VRAM if > 0x100 bytes), eins (drp), overlay
    (-> 0x801C5E00); falls back to slot 1 if eins is < 0x40 bytes or the overlay is empty.  Absolute index 4322+3*eid+k."""
    base = 4322 + 3 * eid
    if base + 2 >= len(fnd):
        return None
    return tuple(fnd[base + k] for k in range(3))


def is_dummy(sl):
    return all(s.startswith("dummy/") or not s for s in sl)


def eff_slots(g, fnd, fnd2=None):
    """{eid: (ctd, prd, prg)} for eid 2..542 whose files are in the archive.  Disc-2 index fills slots that are
    dummy on disc 1 (and overrides slot 378's overlay)."""
    fnd2 = fnd2 or load_fnd(g, "cdrom2.fnd")
    names = set(g.cd.namelist())
    out = {}
    DISC2_ONLY.clear()
    for eid in range(2, 543):
        if eid in IRREGULAR:
            continue
        s1, s2 = slot(fnd, eid), slot(fnd2, eid)
        sl = s1
        if s2 and s2 != s1 and (is_dummy(s1) or eid == 378):
            sl = s2
            DISC2_ONLY.add(eid)
        if sl and not is_dummy(sl) and all(f in names or f.startswith("dummy/") for f in sl):
            out[eid] = sl
    return out


def elements(g):
    rows = []
    for i, r in enumerate(tab.load_elements(g)):
        rows.append((i, g.sysmes(0x17, i), tab.u16(r, 0x10), r[5], r[6], r[7]))
    return rows


# ---------------------------------------------------------------- egfx (.ctd)
def parse_ctd(d):
    """Sections of 1 header sector + data sectors.  Returns [(sector, type, x, y, w_words, h, nsect)]; raises FormatError.
    Header: +0 u32 type, +4 x, +6 y, +8 dx, +10 dy (VRAM x=+4+8, y=+6+10),
    +12 width in 16-bit words, +14 rows/sector, +18 sector count (per section), +20 u32 sections in file, +24 u32 sectors,
    +28 u16 rows[]."""
    if len(d) % SECTOR:
        raise cont.FormatError("ctd size not a sector multiple")
    out, s, total = [], 0, len(d) // SECTOR
    nsec = None
    while s < total:
        h = d[s * SECTOR:(s + 1) * SECTOR]
        ty, x, y, dx, dy, w, rps, _u, _n = struct.unpack_from("<I4H H3H", h, 0)
        sections, sectors = struct.unpack_from("<2I", h, 20)
        if (ty & 0xFFFE) != 0x1200:
            raise cont.FormatError("sector %d: bad section type 0x%X" % (s, ty))
        if nsec is None:
            nsec = sections
        rows = struct.unpack_from("<%dH" % sectors, h, 28) if sectors and 28 + 2 * sectors <= SECTOR else ()
        out.append((s, ty, x + dx, y + dy, w, sum(rows), sectors))
        s += 1 + sectors
    if s != total or len(out) != nsec:
        raise cont.FormatError("sections %d != header %s or size mismatch" % (len(out), nsec))
    return out


# ---------------------------------------------------------------- prd analysis
def analyse_prd(data):
    """Parse a battle effect drp.  Returns dict with entries, emitter/seq op counters, custom ops, errors."""
    es = cont.parse_drp(data)
    r = dict(entries=es, types=collections.Counter(e.type for e in es), emit_ops=collections.Counter(),
             seq_ops=collections.Counter(), emitters=0, seqs=0, custom_ops=collections.Counter(), seq_names=[],
             anims=0, errors=[])
    for e in es:
        try:
            if e.type == 0x0A:
                hd, cmds = prd.parse_effect(e.data)
                r["emitters"] += 1
                for c in cmds:
                    r["emit_ops"][c.op] += 1
            elif e.type == 0x10:
                ops = seq_ops(e.data)
                r["seqs"] += 1
                r["seq_names"].append(e.name)
                for (_p, op, _l, _w, _n) in ops:
                    r["seq_ops"][op] += 1
                    if op >= OVERLAY_OPS_MIN:
                        r["custom_ops"][op] += 1
            elif e.type in (0x19, 0x1A):
                r["anims"] += 1
                cont.parse_cpt(e.data)
        except (prd.StreamError, cont.FormatError, struct.error) as ex:
            r["errors"].append("%s/%02X: %s" % (e.name, e.type, ex))
    return r


def prog_info(g, name):
    d = g.read(name)
    import re
    strs = sorted(set(m.group().decode() for m in re.finditer(rb"[A-Za-z][ -~]{3,}", d) if len(m.group()) < 40))
    return len(d), strs


def build(g):
    """Everything: fnd, slots (both discs), elements, per-slot analysis."""
    fnd = load_fnd(g)
    slots = eff_slots(g, fnd)
    els = elements(g)
    users = collections.defaultdict(list)
    for (i, nm, eid, *_r) in els:
        users[eid].append(i)
    info = {}
    for eid, sl in slots.items():
        a = analyse_prd(g.read(sl[1])) if sl[1].startswith(EFFDIR) else None
        info[eid] = a
    return fnd, slots, els, users, info


def overlay_class(prg, a):
    base = os.path.basename(prg)
    if base in GENERIC_OVERLAYS:
        return "generic-overlay"
    return "custom-overlay"


def classify(prg, a, size):
    """Reproducibility class.  A: data-only (generic host overlay, no overlay-private ops); B: small custom overlay
    (<=1.5 KB, a few functions) with no overlay-private ops; C: large custom overlay or overlay-private ops."""
    base = os.path.basename(prg)
    priv = bool(a and a["custom_ops"])
    if base in GENERIC_OVERLAYS:
        return "A-data" if not priv else "C-hand(private ops on generic host)"
    if size <= 1500 and not priv:
        return "B-small"
    return "C-hand"


# ---------------------------------------------------------------- overlay profile (optional: needs the overlay source folder)
import glob
import re

CALL_RE = re.compile(r"(?:FUN_|func_0x)?(8[0-9a-f]{7})\(|\b(AddPrim|rand|SetRotMatrix|SetTransMatrix|rcos|rsin|ratan2|SquareRoot0|VectorNormal|DpqColor|ApplyMatrixSV|MulMatrix0|RotMatrix|Sound_\w+)\(")
GTE_NAMES = {"SetRotMatrix", "SetTransMatrix", "rcos", "rsin", "ratan2", "SquareRoot0", "VectorNormal", "DpqColor",
             "ApplyMatrixSV", "MulMatrix0", "RotMatrix"}
ADDR_CLASS = {          # main-exe / battle.prg callees seen in the overlays (names from a symbol file or inferred)
    "emitter": {"80040be0", "80040ce4", "80038e60", "80039a00", "80041d80", "80040734", "80041018", "8002a194"},
    "model": {"8009cd9c", "8009cd88", "8009cf54", "8009cf4c", "800a1cd0", "8009ad38", "8009af10", "8009d480",
              "8009e494", "8009e510", "8009c434", "8009f7f4", "800a1c90", "8009cddc", "8009c274", "8009c3b4"},
    "objlist": {"80030938", "8003096c", "800308d0"},
}


_NAMES = {}


def _names(decomp):
    """address -> symbol for the named functions in the source folder's main/ folder (file name NAME__ADDR.c)."""
    if not _NAMES:
        for f in glob.glob(os.path.join(decomp, "main", "*__8*.c")):
            nm, _s, ad = os.path.basename(f)[:-2].rpartition("__")
            if not nm.startswith("FUN_"):
                _NAMES[ad] = nm
    return _NAMES


def overlay_profile(decomp, ov):
    """Counts of callee classes in the overlay source folder.  Returns None if the folder is absent."""
    files = sorted(glob.glob(os.path.join(decomp, ov, "*.c")))
    if not files:
        return None
    pr = collections.Counter()
    for f in files:
        t = open(f, encoding="utf-8", errors="replace").read()
        pr["lines"] += t.count("\n")
        pr["funcs"] += 1
        for m in CALL_RE.finditer(t):
            a, nm = m.group(1), m.group(2)
            if a and not nm:
                nm = _names(decomp).get(a)
            if nm == "AddPrim":
                pr["addprim"] += 1
            elif nm == "rand":
                pr["rand"] += 1
            elif nm in GTE_NAMES:
                pr["gte"] += 1
            elif nm and nm.startswith("Sound_"):
                pr["sound"] += 1
            elif a:
                for k, v in ADDR_CLASS.items():
                    if a in v:
                        pr[k] += 1
        pr["private_cases"] += len(re.findall(r"case 0x1[0-9a-f]{2}:", t))
        pr["setcb"] += t.count("DAT_80071cb4 =")
    return pr


def draws_geometry(pr):
    return bool(pr and pr["addprim"] >= 2)


# ---------------------------------------------------------------- commands
def cmd_map(g, a, out):
    fnd, slots, els, users, info = build(g)
    out.write("# effect-slot base: fid[%d]=%d; slot(eid) = fnd[4322+3*eid + 0..2]  (ctd, prd, prg)\n" % (FID_GROUP, fid_base(g)))
    if a.all:
        out.write("eid\tctd\tprd\tprg\tused_by_elements\n")
        for eid in sorted(slots):
            sl = slots[eid]
            if all(s.startswith("dummy/") for s in sl):
                continue
            out.write("%d\t%s\n" % (eid, "\t".join(sl + (",".join(map(str, users.get(eid, []))),))))
        return
    out.write("elem\tname\teid\tctd\tprd\tprg\tshared_with\n")
    shared = collections.Counter(sl[1] for sl in slots.values() if not sl[1].startswith("dummy/"))
    for (i, nm, eid, col, lv, mode) in els:
        sl = slots.get(eid)
        if eid == 0 or sl is None:
            out.write("%d\t%s\t%d\t-\t-\t-\t\n" % (i, nm, eid))
            continue
        sh = [u for u in users[eid] if u != i]
        out.write("%d\t%s\t%d\t%s\t%s\t%s\t%s\n" % (i, nm, eid, sl[0], sl[1], sl[2], ",".join(map(str, sh))))


def _find(g, fnd, slots, arg):
    if arg.isdigit():
        eid = int(arg)
        return slots[eid][1] if eid in slots else None
    for pre in ("", EFFDIR):
        if g.read and (pre + arg) in g.cd.namelist():
            return pre + arg
    cands = [n for n in g.names(EFFDIR) if n.endswith("/" + arg) or n.endswith("/" + arg + ".prd")]
    return cands[0] if cands else None


def cmd_info(g, a, out):
    fnd = load_fnd(g)
    slots = eff_slots(g, fnd)
    name = _find(g, fnd, slots, a.target)
    if not name:
        out.write("not found: %s\n" % a.target)
        return 1
    d = g.read(name)
    out.write("%s  %d bytes\n" % (name, len(d)))
    eids = [e for e, sl in slots.items() if name in sl]
    if eids:
        for e in eids:
            out.write("effect id %d: ctd=%s prd=%s prg=%s\n" % ((e,) + slots[e]))
    if name.endswith(".ctd"):
        for sec in parse_ctd(d):
            out.write("  sector %3d type 0x%04X vram(%d,%d) w=%d words h=%d data_sectors=%d\n" % sec)
        return 0
    if not name.endswith(".prd"):
        out.write("(no decoder: %s)\n" % os.path.splitext(name)[1])
        return 0
    an = analyse_prd(d)
    for e in an["entries"]:
        extra = ""
        if e.type == 0x0A:
            hd, cmds = prd.parse_effect(e.data)
            extra = "emitter start=%d dur=%d period=%d repeat=%d flags=%s ops=%s" % (
                hd["start"], hd["duration"], hd["period"], hd["repeat"], prd.flags_text(hd["flags"]) + " raw=%08X" % hd["flags"],
                " ".join(prd.EFFECT_OPS[c.op][0] for c in cmds))
        elif e.type == 0x10:
            ops = seq_ops(e.data)
            extra = "sequence: " + " ".join("%s%s" % (SEQ_OPS.get(o, ("OP%X" % o,))[0], ("(%s)" % n) if n else "")
                                          for (_p, o, _l, _w, n) in ops)
        elif e.type in (0x19, 0x1A):
            subs = cont.parse_cpt(e.data)
            extra = "cpt of %d blocks (%s)" % (len(subs), ",".join(str(len(s)) for s in subs[:8]))
        elif e.type == 0x0C:
            extra = "camera record " + " ".join("%d" % struct.unpack_from("<h", e.data, o)[0] for o in range(4, 22, 2))
        elif e.type == 0x16:
            extra = "AKAO sound sequence magic=%r" % e.data[:4]
        out.write("  %3d %-4s %-5s %6d  %s\n" % (e.index, e.name, cont.DRP_TYPES.get(e.type, "t%02X" % e.type), len(e.data), extra))
    for er in an["errors"]:
        out.write("  ERROR %s\n" % er)
    return 0


def cmd_validate(g, a, out):
    fnd = load_fnd(g)
    slots = eff_slots(g, fnd)
    names = g.names(EFFDIR)
    bad, cnt = [], collections.Counter()
    for n in names:
        ext = n.rsplit(".", 1)[-1]
        d = g.read(n)
        try:
            if ext == "prd":
                an = analyse_prd(d)
                if an["errors"]:
                    raise cont.FormatError("; ".join(an["errors"][:2]))
            elif ext == "ctd":
                parse_ctd(d)
            cnt[ext] += 1
        except Exception as ex:                     # report, keep going
            bad.append((n, str(ex)))
    ovl_ok = 0
    for p in sorted(set(sl[2] for sl in slots.values() if sl[2].startswith("prog/"))):
        try:
            sz, _s = prog_info(g, p)
            ovl_ok += sz > 0
        except KeyError:
            bad.append((p, "overlay missing in cdrom.dat"))
    missing = [(e, f) for e, sl in slots.items() for f in sl if not f.startswith("dummy/") and f not in g.cd.namelist()]
    out.write("effects files: %d (%s); failures %d\n" % (len(names), dict(cnt), len(bad)))
    out.write("effect slots (eid) with any file: %d; overlays present: %d; slot paths missing from archive: %d\n" % (
        sum(1 for sl in slots.values() if not all(s.startswith("dummy/") for s in sl)), ovl_ok, len(missing)))
    for b in bad[:30]:
        out.write("  FAIL %s: %s\n" % b)
    # every battle/effects file must be reachable from a slot or be a known satellite
    reach = set(f for sl in slots.values() for f in sl)
    orphans = [n for n in names if n not in reach]
    out.write("effects files not in any slot: %d %s\n" % (len(orphans), orphans[:12]))
    return 1 if bad else 0


def cmd_stats(g, a, out):
    fnd, slots, els, users, info = build(g)
    real = {e: sl for e, sl in slots.items() if not all(s.startswith("dummy/") for s in sl)}
    prgs = collections.Counter(sl[2] for sl in real.values())
    out.write("effect slots: %d (non-dummy); element records %d; elements with effect id %d; distinct effect ids used by elements %d\n" % (
        len(real), len(els), sum(1 for e in els if e[2]), len(set(e[2] for e in els if e[2]))))
    out.write("distinct overlay programs across slots: %d (%d generic hosts)\n" % (
        len(prgs), sum(1 for p in prgs if os.path.basename(p) in GENERIC_OVERLAYS)))
    for p, n in prgs.most_common():
        sz = len(g.read(p)) if p in g.cd.namelist() else 0
        out.write("  %-22s slots=%3d size=%6d\n" % (p, n, sz))
    cls = collections.Counter()
    elcls = collections.Counter()
    fam = collections.defaultdict(list)
    for e, sl in real.items():
        an = info[e]
        sz = len(g.read(sl[2])) if sl[2] in g.cd.namelist() else 0
        c = classify(sl[2], an, sz)
        cls[c] += 1
        for u in users.get(e, []):
            elcls[c] += 1
        if c.startswith(("B", "C")):
            fam[c[0] + ":" + os.path.basename(sl[2])].append(e)
    out.write("\nreproducibility classes (effect slots): %s\n" % dict(sorted(cls.items())))
    out.write("reproducibility classes (element records via their effect id): %s\n" % dict(sorted(elcls.items())))
    seq, emi, cust, typ = collections.Counter(), collections.Counter(), collections.Counter(), collections.Counter()
    nseq = nemit = 0
    for e, an in info.items():
        if an:
            seq.update(an["seq_ops"]); emi.update(an["emit_ops"]); cust.update(an["custom_ops"]); typ.update(an["types"])
            nseq += an["seqs"]; nemit += an["emitters"]
    out.write("\nprd entries by type (slot files): %s\n" % {"0x%02X" % k: v for k, v in sorted(typ.items())})
    out.write("sequences %d, emitters %d\n" % (nseq, nemit))
    out.write("sequence ops: " + " ".join("%02X:%d" % kv for kv in sorted(seq.items())) + "\n")
    out.write("overlay-private ops (>=0x100): " + " ".join("%X:%d" % kv for kv in sorted(cust.items())) + "\n")
    out.write("emitter ops: " + " ".join("%02X:%d" % kv for kv in sorted(emi.items())) + "\n")
    bydir = collections.defaultdict(collections.Counter)
    for e, sl in real.items():
        sz = len(g.read(sl[2])) if sl[2] in g.cd.namelist() else 0
        d = sl[1].split("/")[2] if sl[1].startswith(EFFDIR) else sl[1].split("/")[1]
        bydir[d][classify(sl[2], info[e], sz)[0]] += 1
    out.write("\nclass by effect directory (A data-only / B tiny overlay / C hand port):\n")
    for d, c in sorted(bydir.items()):
        out.write("  %-12s %s\n" % (d, dict(sorted(c.items()))))
    out.write("slots with no egfx (.ctd): %d; disc-2-only slots: %d; max ctd sectors: %d\n" % (
        sum(1 for sl in real.values() if sl[0].startswith("dummy/")), len(DISC2_ONLY),
        max(len(g.read(n)) // SECTOR for n in g.names(EFFDIR) if n.endswith(".ctd"))))
    dec = a.decomp or os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tools", "ghidra", "work", "out", "ps1")
    if os.path.isdir(dec):
        out.write("\noverlay profile (decomp lines/funcs; addprim = custom polygon emission; gte = GTE/trig helper calls; "
                  "emit/model/objl = engine API calls; c1xx = overlay-private op cases):\n")
        out.write("  %-12s %5s %4s %7s %4s %5s %5s %5s %5s %5s %s\n" % (
            "overlay", "lines", "fns", "addprim", "gte", "emit", "model", "objl", "rand", "c1xx", "eids"))
        byov = collections.defaultdict(list)
        for e, sl in real.items():
            byov[os.path.basename(sl[2])[:-4]].append(e)
        gcount = collections.Counter()
        for o, es in sorted(byov.items()):
            if (o + ".prg") in GENERIC_OVERLAYS:
                continue
            pr = overlay_profile(dec, o)
            if pr is None:
                continue
            geo = draws_geometry(pr)
            gcount["draws-prims" if geo else "no-prims"] += 1
            out.write("  %-12s %5d %4d %7d %4d %5d %5d %5d %5d %5d %s%s\n" % (
                o, pr["lines"], pr["funcs"], pr["addprim"], pr["gte"], pr["emitter"], pr["model"], pr["objlist"],
                pr["rand"], pr["private_cases"], ",".join(map(str, es[:6])), "" if geo else "  [no AddPrim]"))
        out.write("custom overlays: %s\n" % dict(gcount))
    out.write("\nhand-port families (slots by overlay):\n")
    for k, v in sorted(fam.items()):
        out.write("  %-22s eids=%s\n" % (k, v))
    return 0


COMMANDS = dict(map=cmd_map, info=cmd_info, validate=cmd_validate, stats=cmd_stats)


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", default=None, help="RDE install dir (default: parent of OpenCross)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("map"); p.add_argument("--all", action="store_true")
    p = sub.add_parser("info"); p.add_argument("target")
    sub.add_parser("validate")
    p = sub.add_parser("stats"); p.add_argument("--decomp", default=None, help="your own PS1 decompilation export, the <OutDir>/ps1 folder written by tools/ghidra_setup.ps1 (default: tools/ghidra/work/out/ps1); the overlay profile is skipped when absent")
    a = ap.parse_args(argv)
    g = tab.Game(a.game)
    return COMMANDS[a.cmd](g, a, sys.stdout) or 0


if __name__ == "__main__":
    sys.exit(main())
