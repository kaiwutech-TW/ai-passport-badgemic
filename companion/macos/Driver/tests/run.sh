#!/bin/bash
# Copyright (c) 2026 BadgeMic contributors. MIT License.
# Builds the driver (via ../build.sh) and runs the host-side harness.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
"$HERE/../build.sh"
BIN="$HERE/../../dist/BadgeMicAudio.driver/Contents/MacOS/BadgeMicAudio"
OUT="${TMPDIR:-/tmp}/badgemic_test_driver"
clang -std=c11 -Wall -Wextra -Wno-nonnull -O1 -framework CoreAudio -framework CoreFoundation \
    -o "$OUT" "$HERE/test_driver.c"
"$OUT" "$BIN"
