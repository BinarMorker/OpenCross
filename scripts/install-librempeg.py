"""Installs a built librempeg tree (static MSVC libs + public headers) into a prefix.

usage: python install-librempeg.py <librempeg source dir> <prefix>
`make install` can't be used with native mingw32-make (its header-install recipe breaks the shell quoting).
Public headers are the HEADERS lists of each library's Makefile, plus generated avconfig.h / ffversion.h.
"""
import pathlib
import re
import shutil
import sys

src = pathlib.Path(sys.argv[1])
prefix = pathlib.Path(sys.argv[2])
(prefix / "lib").mkdir(parents=True, exist_ok=True)

for lib in ("avutil", "avfilter", "avcodec", "avformat"):
    d = src / f"lib{lib}"
    text = (d / "Makefile").read_text(encoding="utf-8").replace("\\\n", " ")
    headers = []
    for m in re.finditer(r"^(?:HEADERS|BUILT_HEADERS)\s*[+:]?=(.*)$", text, re.M):
        headers += m.group(1).split()
    out = prefix / "include" / f"lib{lib}"
    out.mkdir(parents=True, exist_ok=True)
    for h in headers:
        shutil.copy2(d / h, out / h)
    built = d / f"{lib}.lib"
    if not built.exists():
        built = d / f"lib{lib}.a"
    shutil.copy2(built, prefix / "lib" / f"{lib}.lib")
    print(f"lib{lib}: {len(headers)} headers, {built.name} -> lib/{lib}.lib")
