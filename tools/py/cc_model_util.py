"""Small container helpers for cc_model.py (cpt / drp / archive access).

Self-contained on purpose (standard library only) so the model prototype does
not depend on the other tools in this directory.
"""
import struct
import zipfile

GAME_DIR = r"C:\Program Files (x86)\Steam\steamapps\common\CHRONO CROSS  THE RADICAL DREAMERS EDITION"
CDROM_DAT = GAME_DIR + r"\data\cdrom.dat"


def u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def s16(d, o):
    return struct.unpack_from("<h", d, o)[0]


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def s32(d, o):
    return struct.unpack_from("<i", d, o)[0]


class Archive:
    """Read-only view of cdrom.dat (a plain ZIP)."""

    def __init__(self, path=CDROM_DAT):
        self.z = zipfile.ZipFile(path)
        self.names = [n for n in self.z.namelist() if not n.endswith("/")]

    def read(self, name):
        return self.z.read(name)

    def model_files(self):
        """Return {class: [paths]} for every model-bearing file in the game."""
        cls = {"battle_player_obj": [], "battle_monster_obj": [], "field_obj": [],
               "field_ex_bin": [], "weapon_bin": [], "weapon_kmd": [],
               "atim_ltd": [], "atim_ltc": [], "field_tim": [], "anim_prd": []}
        for n in self.names:
            if n.startswith("battle/player/") and n.endswith(".obj"):
                cls["battle_player_obj"].append(n)
            elif n.startswith("battle/monster/") and n.endswith(".obj"):
                cls["battle_monster_obj"].append(n)
            elif n.startswith("fieldobj/player/") and n.endswith(".obj"):
                cls["field_obj"].append(n)
            elif n.startswith("fieldobj/ex/") and n.endswith(".bin"):
                cls["field_ex_bin"].append(n)
            elif n.startswith("battle/wepon/") and n.endswith(".bin"):
                cls["weapon_bin"].append(n)
            elif n.startswith("battle/wepon/") and n.endswith(".kmd"):
                cls["weapon_kmd"].append(n)
            elif n.startswith(("battle/player/", "battle/monster/")) and n.endswith(".ltd"):
                cls["atim_ltd"].append(n)
            elif n.startswith(("battle/player/", "battle/monster/")) and n.endswith(".ltc"):
                cls["atim_ltc"].append(n)
            elif n.startswith("fieldobj/player/") and n.endswith(".tim"):
                cls["field_tim"].append(n)
            elif n.startswith(("battle/player/", "battle/monster/")) and n.endswith(".prd"):
                cls["anim_prd"].append(n)
        return cls


def cpt_entries(d, base=0, limit=None):
    """Split a cpt: u32 count, count x u32 offsets (relative to `base`), optional EOF pointer.

    Returns [(start, end)] absolute offsets into d.  The EOF pointer is detected
    (the offset table is followed by one extra pointer == the end of the block).
    `limit` is the block end when known (needed for the last entry of EOF-less cpts).
    """
    n = u32(d, base)
    if n == 0 or n > 4096:
        raise ValueError("bad cpt count %d" % n)
    ptrs = [u32(d, base + 4 + 4 * i) for i in range(n)]
    nxt = u32(d, base + 4 + 4 * n) if base + 8 + 4 * n <= len(d) else 0
    first = min(p for p in ptrs if p) if any(ptrs) else 0
    has_eof = (base + 4 + 4 * (n + 1) <= base + first) and nxt >= ptrs[-1]
    ends_src = ptrs + ([nxt] if has_eof else [])
    out = []
    for i, p in enumerate(ptrs):
        if i + 1 < len(ends_src):
            e = ends_src[i + 1]
        else:
            e = (limit - base) if limit is not None else len(d) - base
        out.append((base + p, base + e))
    return out


def drp_entries(d):
    """Parse a DRP ("drp\0" magic).  Returns [(name, type, start, end)] (data excludes the 12-byte entry header)."""
    if d[:4] != b"drp\0":
        raise ValueError("not a drp")
    num = u16(d, 8) // 64
    ptrs = [u32(d, 12 + 4 * i) for i in range(num)]
    out = []
    for i, p in enumerate(ptrs):
        name = d[p + 4:p + 8].rstrip(b"\0").decode("latin1")
        typ = d[p + 8]
        ln = int.from_bytes(d[p + 9:p + 12], "little") >> 4   # stored value = 16 * length
        out.append((name, typ, p + 12, p + 12 + ln))
    return out
