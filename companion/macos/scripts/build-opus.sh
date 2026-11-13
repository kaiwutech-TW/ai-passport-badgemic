#!/usr/bin/env bash
# Builds libopus.0.dylib for the app bundle with a macOS 14 deployment target.
# Homebrew's bottle targets the build machine's macOS (e.g. 27), so bundling it would make
# the app fail to launch on older Macs. Same version as Homebrew's opus, so the headers the
# app compiles against (via pkg-config) match. Output: .build/opus-bundle/libopus.0.dylib
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER="1.6.1"
SHA256="6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1"
URL="https://ftp.osuosl.org/pub/xiph/releases/opus/opus-$VER.tar.gz"
MIN="${MACOSX_DEPLOYMENT_TARGET:-14.0}"
OUT="$ROOT/.build/opus-bundle"
LIB="$OUT/libopus.0.dylib"

if [ -f "$LIB" ] && otool -l "$LIB" | grep -A4 LC_BUILD_VERSION | grep -q "minos $MIN"; then
  echo "$LIB"; exit 0
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
curl -fsSL -o "$WORK/opus.tar.gz" "$URL"
echo "$SHA256  $WORK/opus.tar.gz" | shasum -a 256 -c - >/dev/null
tar -xzf "$WORK/opus.tar.gz" -C "$WORK"
(
  cd "$WORK/opus-$VER"
  MACOSX_DEPLOYMENT_TARGET="$MIN" ./configure --disable-static --enable-shared --disable-doc \
    --disable-extra-programs --prefix="$WORK/inst" CFLAGS="-O2 -mmacosx-version-min=$MIN" >/dev/null
  make -j"$(sysctl -n hw.ncpu)" >/dev/null
  make install >/dev/null
)
mkdir -p "$OUT"
cp "$(readlink -f "$WORK/inst/lib/libopus.0.dylib")" "$LIB"
chmod u+w "$LIB"
install_name_tool -id "@rpath/libopus.0.dylib" "$LIB"
echo "$LIB"
