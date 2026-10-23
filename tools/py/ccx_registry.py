r"""The ccx effect registry (global resource list) at the first frames of a battle, per scene.

    python tools/py/ccx_registry.py [--jobs 5] [--frames 200] [--every 40] [--out DIR] SCENE [SCENE ...]   (SCENE or A-B range)

One short ccx run per scene: poke battle id 0x200|scene into tmp\ref\states\field53_encounter_p.sav (uncompressed copy built once,
patched in Python), run FRAMES frames writing a state every EVERY frames, read the registry of every state, delete the states.
Output: DIR\<scene>.txt (text only): per state "# frame F btl_frame B ovl O n N" then "type name addr" lines of the registry.
No mash, no extra pokes: the registry of the untouched battle start (the stage = the area of the base state).
Run from a .claude\worktrees checkout: the saves / states / ccx come from MAIN (the OpenCross checkout).
"""
import argparse
import concurrent.futures as cf
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ccx_state as cs  # noqa: E402
from fx_ref import Registry  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
m = re.match(r"^(.*?)[\\/]\.claude[\\/]worktrees[\\/][^\\/]+$", ROOT)
MAIN = m.group(1) if m else ROOT
GAME = os.path.dirname(MAIN)
CCX = os.path.join(GAME, "rde-reimpl", "bin", "ccx", "ccx.exe")
ENC_STATE = os.path.join(MAIN, "tmp", "ref", "states", "field53_encounter_p.sav")
ENC_SAVE = os.path.join(MAIN, "tmp", "ref", "save_p")


def run_ccx(logdir, extra, save_src):
    shutil.rmtree(logdir, ignore_errors=True)
    sv = logdir + ".sv"
    shutil.rmtree(sv, ignore_errors=True)
    shutil.copytree(save_src, sv)
    args = [CCX, "-uncapped", "-set", "GPU/ResolutionScale=2", "-set", "CCX/Movies=false", "-set", "CCX/GameDir=" + GAME,
            "-set", "CCX/SaveDir=" + sv, "-set", "InputSources/XInput=false", "-set", "InputSources/SDL=false",
            "-set", "Main/StartFullscreen=false", "-logdir", logdir] + extra
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 7  # minimised, no activation
    p = subprocess.Popen(args, cwd=os.path.dirname(CCX), startupinfo=si, creationflags=0x00004000)  # BELOW_NORMAL
    rc = p.wait()
    shutil.rmtree(sv, ignore_errors=True)
    return rc


def one(scene, base, work, out, frames, every, mash=False, disc2=False, keep=False):
    dst = os.path.join(out, "%d.txt" % scene)
    if os.path.exists(dst) and os.path.getsize(dst) > 0:
        return scene, "skip"
    poked = os.path.join(work, "s%d.sav" % scene)
    data = bytearray(open(base, "rb").read())
    off = cs.State(base).ram_off + 0x75906
    data[off:off + 2] = (0x200 | scene).to_bytes(2, "little")
    if disc2:  # disc 2 = the game's own ChangeDisc: cdrom2.fnd into the index buffer 0x80780000, u8@0x800720FE = 2
        import zipfile
        fnd2 = zipfile.ZipFile(os.path.join(GAME, "data", "cdrom.dat")).read("cdrom2.fnd")
        data[cs.State(base).ram_off + 0x780000:cs.State(base).ram_off + 0x780000 + len(fnd2)] = fnd2
        data[cs.State(base).ram_off + 0x720FE] = 2
        data[cs.State(base).ram_off + 0xEBAEA] = 1  # script var 0x42 = disc - 1; the disc check would swap back otherwise
    open(poked, "wb").write(data)
    log = os.path.join(work, "log%d" % scene)
    rc = run_ccx(log, ["-state", poked, "-set", "Main/SaveStateCompression=Uncompressed", "-frames", str(frames),
                       "-stateevery", str(every), str(every), str(frames)]
                + (["-mashhold", "4", "-mash", "Cross", "30", "10", str(frames)] if mash else []), ENC_SAVE)
    lines = []
    sd = os.path.join(log, "states")
    for p in cs.list_states([sd]) if os.path.isdir(sd) else []:
        st = cs.State(p)
        w = st.watch()
        reg = Registry(st).nodes
        lines.append("# frame %d btl_frame %d ovl %d n %d" % (cs.frame_of(p), w["btl_frame"], w["ovl_id"], len(reg)))
        for (a, t, nm, d, typ) in reg:
            lines.append("%d\t%s\t%x" % (t, nm, a))
    if not keep:
        shutil.rmtree(log, ignore_errors=True)
    os.remove(poked)
    with open(dst, "w", encoding="utf8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return scene, "rc=%d states=%d" % (rc, sum(1 for l in lines if l.startswith("#")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenes", nargs="+")
    ap.add_argument("--mash", action="store_true", help="press Cross every 30 frames (scenes with a scripted intro)")
    ap.add_argument("--disc2", action="store_true", help="switch the state to disc 2 (cdrom2.fnd) before the battle starts")
    ap.add_argument("--keep", action="store_true", help="keep the ccx log dir (states) under DIR/../_work/log<scene>")
    ap.add_argument("--jobs", type=int, default=5)
    ap.add_argument("--frames", type=int, default=200)
    ap.add_argument("--every", type=int, default=40)
    ap.add_argument("--out", default=os.path.join(MAIN, "tmp", "r38", "D2", "ccx"))
    a = ap.parse_args()
    scenes = []
    for s in a.scenes:
        if "-" in s:
            lo, hi = s.split("-")
            scenes += list(range(int(lo), int(hi) + 1))
        else:
            scenes.append(int(s))
    os.makedirs(a.out, exist_ok=True)
    work = os.path.join(MAIN, "tmp", "r38", "D2", "_work")
    os.makedirs(work, exist_ok=True)
    base = os.path.join(work, "enc_unc.sav")
    if not os.path.exists(base):
        tmp = os.path.join(work, "unc")
        run_ccx(tmp, ["-state", ENC_STATE, "-set", "Main/SaveStateCompression=Uncompressed", "-frames", "3", "-stateevery", "1", "1", "2"], ENC_SAVE)
        shutil.copyfile(os.path.join(tmp, "states", "state_000001.sav"), base)
        shutil.rmtree(tmp, ignore_errors=True)
    with cf.ThreadPoolExecutor(a.jobs) as ex:
        futs = [ex.submit(one, s, base, work, a.out, a.frames, a.every, a.mash, a.disc2, a.keep) for s in scenes]
        for f in cf.as_completed(futs):
            print(*f.result(), flush=True)


if __name__ == "__main__":
    main()
