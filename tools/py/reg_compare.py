r"""ps1 registry (tools/py/ps1_registry.py output) vs ours (ocbattle fx-reg output), per scene.

    python tools/py/reg_compare.py REFDIR OURSDIR [--index INDEXFILE] [--md OUT.md] [--tsv OUT.tsv]

REFDIR/<scene>.txt = "# frame F btl_frame B ovl O n N" blocks + "type<TAB>name<TAB>addr" lines.  The snapshot used is the LAST state of the
run (the registry only grows during the first frames; the earlier snapshots are compared to it for growth).
OURSDIR/<scene>.txt = `ocbattle fx-reg SCENE` lines "type<TAB>name<TAB>count<TAB>sources".
INDEXFILE = `ocbattle fx-reg --index`: "type<TAB>name<TAB>file,file" (where a missing name does live).
Prints, per scene: ps1-only (multiset difference ps1 - ours), ours-only, and the index sources of every ps1-only name.
"""
import argparse
import collections
import os
import sys


# Registry nodes that are not file entries: ps1 derives 14 (mesh / sprite caches), 15 / 17 / 19 (screen layers, light, ring) at boot; ours lists
# type 4 (TIM payloads, no registry node), 5 and 18 (wave / ring data) as drp entries. Both sides drop them.
IGN_REF = {14, 15, 17, 19}
IGN_OURS = {4, 5, 18}


def norm(k):
    return (k[0], k[1].rstrip("."))   # ps1 prints NUL as '.'


def load_ps1(path):
    snaps, cur = [], None
    for line in open(path, encoding="utf8"):
        line = line.rstrip("\n")
        if line.startswith("#"):
            f = line.split()
            cur = {"frame": int(f[2]), "btl": int(f[4]), "ovl": int(f[6]), "c": collections.Counter()}
            snaps.append(cur)
        elif line and cur is not None:
            t, nm, _ = line.split("\t")
            if int(t) not in IGN_REF:
                cur["c"][norm((int(t), nm))] += 1
    return snaps


def load_ours(path):
    c, src = collections.Counter(), {}
    for line in open(path, encoding="utf8"):
        p = line.rstrip("\n").split("\t")
        if len(p) < 4:
            continue
        if int(p[0]) in IGN_OURS:
            continue
        c[norm((int(p[0]), p[1]))] += int(p[2])
        src[norm((int(p[0]), p[1]))] = p[3]
    return c, src


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ps1")
    ap.add_argument("ours")
    ap.add_argument("--index")
    ap.add_argument("--tsv")
    ap.add_argument("--frame", type=int, default=0, help="use the battle state at this ps1 frame (default: the last)")
    a = ap.parse_args()
    idx = {}
    if a.index:
        for line in open(a.index, encoding="utf-8-sig"):
            p = line.rstrip("\n").split("\t")
            if len(p) == 3:
                idx[norm((int(p[0]), p[1]))] = p[2]
    rows = []
    for fn in sorted(os.listdir(a.ps1), key=lambda x: int(os.path.splitext(x)[0]) if x[:-4].isdigit() else 0):
        if not fn.endswith(".txt") or not fn[:-4].isdigit():
            continue
        scene = int(fn[:-4])
        op = os.path.join(a.ours, fn)
        if not os.path.exists(op):
            continue
        snaps = [s for s in load_ps1(os.path.join(a.ps1, fn)) if s["ovl"] == 8]
        if not snaps:
            rows.append((scene, None, None, None, None))
            continue
        last = next((x for x in snaps if x["frame"] == a.frame), snaps[-1]) if a.frame else snaps[-1]
        ours, src = load_ours(op)
        cx = collections.Counter({k: v for k, v in (last["c"] - ours).items()})
        ox = collections.Counter({k: v for k, v in (ours - last["c"]).items()})
        growth = snaps[-1]["c"] - snaps[0]["c"]
        rows.append((scene, last, cx, ox, growth))
    out = []
    for scene, last, cx, ox, growth in rows:
        if last is None:
            out.append("scene %d: ps1 never reached the battle overlay in the run" % scene)
            continue
        def fmt(c):
            return " ".join("%d:%s%s" % (k[0], k[1], "x%d" % v if v > 1 else "") for k, v in sorted(c.items()))
        out.append("scene %d (ps1 n=%d at frame %d, btl %d): ps1-only [%s] ours-only [%s] grew-after-first-state [%s]" % (
            scene, sum(last["c"].values()), last["frame"], last["btl"], fmt(cx), fmt(ox), fmt(growth)))
        for k in sorted(cx):
            out.append("    %d:%s ps1+%d  index: %s" % (k[0], k[1], cx[k], idx.get(k, "NOT IN ANY FILE")))
    print("\n".join(out))
    if a.tsv:
        with open(a.tsv, "w", encoding="utf8") as f:
            for scene, last, cx, ox, growth in rows:
                if last is None:
                    f.write("%d\tDIFF\tD2 ps1 never reached battle\n" % scene)
                    continue
                f.write("%d\t%s\tD2 ps1-only %d names (%d entries), ours-only %d\n" % (
                    scene, "MATCH" if not cx and not ox else "DIFF", len(cx), sum(cx.values()), len(ox)))


if __name__ == "__main__":
    main()
