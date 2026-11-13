#!/bin/bash
# Copyright (c) 2026 BadgeMic contributors. MIT License.
# Builds and code-signs BadgeMicAudio.driver into ../dist/.
# Signing identity: $BADGEMIC_SIGN_IDENTITY (hash or name), else the first
# "Developer ID Application", else the first "Apple Development", else ad-hoc.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/../dist/BadgeMicAudio.driver"

rm -rf "$OUT"
mkdir -p "$OUT/Contents/MacOS"
cp "$HERE/Info.plist" "$OUT/Contents/Info.plist"

clang -bundle -arch arm64 -arch x86_64 -mmacosx-version-min=13.0 \
    -std=c11 -O2 -Wall -Wextra -fvisibility=hidden \
    -framework CoreAudio -framework CoreFoundation \
    -o "$OUT/Contents/MacOS/BadgeMicAudio" "$HERE/BadgeMicAudio.c"

pick_identity() {
    if [ -n "${BADGEMIC_SIGN_IDENTITY:-}" ]; then echo "$BADGEMIC_SIGN_IDENTITY"; return; fi
    local list; list="$(security find-identity -v -p codesigning 2>/dev/null || true)"
    local h
    h="$(echo "$list" | awk '/"Developer ID Application/ {print $2; exit}')"
    [ -z "$h" ] && h="$(echo "$list" | awk '/"Apple Development/ {print $2; exit}')"
    echo "${h:--}"
}
ID="$(pick_identity)"
if [ "$ID" = "-" ]; then echo "signing: ad-hoc"; else echo "signing: identity $ID"; fi
# Hardened runtime + secure timestamp are required for notarization (Developer ID);
# a timestamp is skipped for other identities so offline builds keep working.
OPTS=()
if [ "$ID" != "-" ]; then
  if security find-identity -v -p codesigning | grep -F "$ID" | grep -q "Developer ID Application"; then
    OPTS=(--options runtime --timestamp)
  else
    OPTS=(--options runtime --timestamp=none)
  fi
fi
codesign --force --sign "$ID" ${OPTS[@]+"${OPTS[@]}"} "$OUT"
codesign --verify --verbose=2 "$OUT"
echo "built: $OUT"
