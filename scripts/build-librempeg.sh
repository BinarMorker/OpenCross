#!/bin/bash
# Called by build-librempeg.ps1 inside a vcvars64 environment. Args: <repo root> <librempeg git url>.
# Builds only what the movies need: bink demuxer, bink2 video, Bink audio (DCT/RDFT) decoders; static, /MT (matches the project: static CRT, one exe).
set -euo pipefail
LIBREMPEG_COMMIT=25d1ead99068ffb1f11df19cf0707854603d621e
ROOT=$(cygpath -u "$1")
URL=$2
BUILD="$ROOT/build/librempeg-src"
SRC="$BUILD/librempeg"
PREFIX="${LIBREMPEG_PREFIX:-$ROOT/third_party/librempeg}"
export PATH="$(cygpath -u "$VCToolsInstallDir")/bin/Hostx64/x64:$PATH"  # MSVC link.exe must win over /usr/bin/link
command -v nasm >/dev/null || { echo "nasm not found (set NASM_DIR)"; exit 1; }
MAKE=$(command -v make || command -v mingw32-make || true)
[ -n "$MAKE" ] || { echo "GNU make not found (set MAKE_DIR)"; exit 1; }
mkdir -p "$BUILD"
[ -d "$SRC/.git" ] || git clone --no-checkout "$URL" "$SRC"
cd "$SRC"
git checkout -q -f "$LIBREMPEG_COMMIT"
git clean -qfdx
export TMPDIR="$BUILD/tmp"; mkdir -p "$TMPDIR"
# (prefix is irrelevant: we install with install-librempeg.py; a path with spaces breaks the generated .pc rules)
./configure --toolchain=msvc --arch=x86_64 --target-os=win64 --prefix=/librempeg \
  --enable-gpl --enable-version3 --enable-static --disable-shared \
  --disable-programs --disable-doc --disable-network --disable-autodetect --enable-w32threads \
  --disable-everything --disable-avdevice --disable-swscale \
  --enable-demuxer=bink --enable-decoder=bink2,binkaudio_dct,binkaudio_rdft \
  --extra-cflags="-MT" > "$BUILD/configure.log" 2>&1 || { tail -30 "$BUILD/configure.log"; exit 1; }
# libavcodec depends on libavfilter in librempeg: disabling avfilter would silently disable avcodec/avformat.
grep -q '^CONFIG_AVCODEC=yes' ffbuild/config.mak || { echo "configure disabled libavcodec"; exit 1; }
"$MAKE" -j"$(nproc)" > "$BUILD/build.log" 2>&1 || { tail -40 "$BUILD/build.log"; exit 1; }
python "$ROOT/scripts/install-librempeg.py" "$SRC" "$PREFIX"
cp -f COPYING.GPLv3 "$PREFIX/COPYING.GPLv3"
echo "librempeg $LIBREMPEG_COMMIT ($URL)" > "$PREFIX/version.txt"
