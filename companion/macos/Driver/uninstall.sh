#!/bin/bash
# Copyright (c) 2026 BadgeMic contributors. MIT License.
# Removes BadgeMicAudio.driver and restarts coreaudiod. Requires administrator rights.
set -euo pipefail
sudo rm -rf "/Library/Audio/Plug-Ins/HAL/BadgeMicAudio.driver"
echo "restarting coreaudiod..."
sudo killall coreaudiod 2>/dev/null \
  || sudo launchctl kickstart -kp system/com.apple.audio.coreaudiod
echo "removed."
