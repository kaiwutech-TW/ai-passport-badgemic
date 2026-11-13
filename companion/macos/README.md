<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# BadgeMic (macOS companion)

A menu-bar app for the Badge Mic firmware on the FoloToy AI Passport. The badge is a BLE
push-to-talk microphone and name badge; this app:

1. receives the badge audio (Opus over BLE), plays it into a **virtual microphone** (e.g. BlackHole 2ch) while
   holding your dictation tool's hotkey, so tools such as Parroting or Doubao type the text;
2. maps the UP/DOWN buttons to shortcuts, text snippets, Shortcuts.app shortcuts or apps;
3. renders and uploads the badge pages (photo/name/title card and a QR card) as 240x320 RGB565 images;
4. controls the display mode (auto / always on, dim and sleep timers, brightness).

The wire contract is [`docs/development/engineering/badge-mic-protocol.md`](../../docs/development/engineering/badge-mic-protocol.md).

## Requirements

- macOS 14+, Command Line Tools (Xcode is **not** required), Swift 6 toolchain.
- Homebrew `opus` (`brew install opus`) for libopus.
- A virtual microphone driver, e.g. `brew install blackhole-2ch`.

## Build

```sh
cd companion/macos
./scripts/build-app.sh        # -> dist/BadgeMic.app (release, libopus + virtual mic driver bundled, signed)
./scripts/package.sh          # -> dist/BadgeMic-<version>.dmg (Developer ID, notarized and stapled)
./scripts/test.sh             # run the Swift Testing suite (retries a known CLT flake)
swift build                   # plain debug build
```

Signing: the script signs with `$BADGEMIC_SIGN_IDENTITY` (a certificate SHA-1 hash or name) if set, otherwise the
first "Developer ID Application" identity, otherwise the first "Apple Development" identity (hardened runtime,
entitlements in `scripts/BadgeMic.entitlements`). Only when none exists does it fall back to ad-hoc signing, where the
designated requirement is the code hash and macOS forgets the Accessibility grant on every rebuild. Check with
`codesign -d -r- dist/BadgeMic.app`. A plain `build-app.sh` build is not notarized, so `spctl` rejects it.

Distribution: `package.sh` needs a "Developer ID Application" identity and a notarytool keychain profile named
`BadgeMic` (override with `$NOTARY_PROFILE`), created once with
`xcrun notarytool store-credentials BadgeMic --apple-id <id> --team-id <team>` and an app-specific password.
It notarizes and staples both the app and the DMG. libopus is built from source for macOS 14+ by
`scripts/build-opus.sh` (Homebrew's bottle targets the build machine's macOS). The app installs the bundled
driver itself (Dictation tab, administrator password). The binary is arm64 only.

Tests use Swift Testing (XCTest is not available with Command Line Tools only). `test.sh`
retries because under Command Line Tools `swift test` sometimes fails to locate the
Swift Testing macro plugin (nondeterministic; the retry succeeds).

Layout: `BadgeMicCore` (framing, protocol, CRC-32, RGB565, upload planner, push-to-talk state machine, models; no
AppKit/CoreBluetooth), `BadgeMicOpus` (libopus wrapper), `BadgeMicRender` (page renderer), `BadgeMic` (BLE, audio
output, key injection, SwiftUI).

## Permissions and setup

1. **Bluetooth**: allow it when macOS asks. On first connection macOS shows a pairing dialog; type the 6-digit
   passkey shown on the badge. The characteristics require encryption.
2. **Accessibility** (System Settings > Privacy & Security > Accessibility): required to inject the hotkey. The
   Dictation tab shows the status. Restart the app after granting it.
3. **Virtual microphone**: install the BadgeMic virtual microphone driver (device "BadgeMic Microphone"; BlackHole 2ch also works). Auto-detect prefers BadgeMic, then BlackHole and similar; if nothing is found the audio is refused instead of played to the speakers. In BadgeMic's Dictation tab choose it as the output device. In your
   dictation tool choose **BlackHole 2ch** as the microphone (or enable "switch system input" in the profile so
   BadgeMic makes it the default input only during a turn and restores the previous one afterwards).
4. Pick the trigger profile (Parroting: Fn hold; Doubao: Fn hold; Custom: any key + modifiers, hold or toggle).
   Use "Test tone" and "Test hotkey" to check routing without the badge.

Safety: the hotkey is released on `ptt_end`, on BLE disconnect, after 2 s without audio/events during a turn, and on
quit (including SIGTERM/SIGINT).

## Usage

- Hold **OK** on the badge: audio streams to the Mac and the dictation tool types. A tap on OK switches the badge page.
- **Buttons** tab: UP/DOWN click and long press actions.
- **Badge** tab: edit fields, choose a photo, choose vCard or custom QR content, check the live preview, upload.
- **Device** tab: connection, battery, firmware, last turn statistics, display settings (only changed fields are sent), microphone gain, and per-turn encoder/clipping diagnostics.

## License

MIT, see [LICENSE](LICENSE). The code is an independent implementation; no GPL code was used.
