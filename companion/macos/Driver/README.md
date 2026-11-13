<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# BadgeMic virtual audio driver

`BadgeMicAudio.driver` is a minimal macOS Core Audio loopback device (an
AudioServerPlugIn, loaded by `coreaudiod`). It exposes one device,
**BadgeMic Microphone**, with one 2-channel 32-bit float output stream and one
2-channel 32-bit float input stream at 48000 Hz (44100 Hz is also available).
Audio written to the output side is returned on the input side through a
~1.4 second ring buffer indexed by device sample time; unwritten time reads
as silence.

The BadgeMic app plays badge audio into the output side, and dictation apps
pick "BadgeMic Microphone" as their input. The device never becomes the
default or system output device; it can be selected as an input device.

Device UID `BadgeMicLoopback_UID`, model UID `BadgeMicLoopback_ModelUID`,
manufacturer `BadgeMic`, bundle id `tw.kaiwu.badgemic.audio`.

## Build

Needs only the Xcode Command Line Tools (no Xcode).

    ./build.sh

Output: `../dist/BadgeMicAudio.driver` (universal arm64 + x86_64, macOS 13+).
Signing identity, in order: `$BADGEMIC_SIGN_IDENTITY` (hash or name), the
first "Developer ID Application" identity, the first "Apple Development"
identity, otherwise ad-hoc.

## Test without installing

    ./tests/run.sh

Builds the driver, then loads the binary with `dlopen`, drives it through a
fake host interface and checks properties, stream formats, zero timestamps,
sample-rate changes and a write/read loopback of a sine wave.

## Install / uninstall

Installing a system audio driver needs administrator rights (sudo).

    ./install.sh      # copies to /Library/Audio/Plug-Ins/HAL and restarts coreaudiod
    ./uninstall.sh    # removes it and restarts coreaudiod

Restarting `coreaudiod` briefly interrupts all audio on the Mac. If
`killall coreaudiod` is not permitted, the scripts fall back to
`launchctl kickstart -kp system/com.apple.audio.coreaudiod`.

## Clean-room note

This driver was written from scratch using only Apple's public SDK headers
(`CoreAudio/AudioServerPlugIn.h`, `AudioHardwareBase.h`, ...) and Apple
documentation. No third-party driver source (including GPL-licensed
projects) was read or copied. It is released under the MIT license.

## Source layout

- `BadgeMicAudio.c` - the whole plug-in (plain C)
- `Info.plist` - bundle metadata and CFPlugIn factory registration
- `build.sh`, `install.sh`, `uninstall.sh`
- `tests/test_driver.c`, `tests/run.sh` - host-side test harness
