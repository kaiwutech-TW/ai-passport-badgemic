#!/bin/bash
# Copyright (c) 2026 BadgeMic contributors. MIT License.
# Installs BadgeMicAudio.driver system-wide. Requires administrator rights.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="${1:-$HERE/../dist/BadgeMicAudio.driver}"
DEST="/Library/Audio/Plug-Ins/HAL"
[ -d "$SRC" ] || { echo "not found: $SRC (run build.sh first)"; exit 1; }

sudo mkdir -p "$DEST"
sudo rm -rf "$DEST/BadgeMicAudio.driver"
sudo cp -R "$SRC" "$DEST/"
sudo chown -R root:wheel "$DEST/BadgeMicAudio.driver"

echo "restarting coreaudiod..."
sudo killall coreaudiod 2>/dev/null \
  || sudo launchctl kickstart -kp system/com.apple.audio.coreaudiod
echo "done. Look for 'BadgeMic Microphone' in System Settings > Sound > Input."
