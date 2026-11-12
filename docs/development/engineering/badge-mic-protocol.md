<p align="right">
  <a href="badge-mic-protocol.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Badge Mic Wire Protocol (Device to Mac)

This document is the contract between the Badge Mic firmware (`main/` on
`feature/badge-mic`) and the macOS companion app (`companion/macos/`). Change both
sides in the same iteration. The framing is adapted from the MIT-licensed
Pocket Intercom protocol by Shinku-Chen
(`Shinku-Chen/ai-passport`, branch `feature/openclaw-intercom`,
`docs/development/engineering/intercom-wire-protocol.md`).

## Product behavior this protocol serves

- The device is a portrait name badge. The Mac renders every badge page as a
  240 × 320 image and uploads it; the device never lays out user text, so any
  font, script, or QR code works without on-device glyph coverage.
- Holding **OK** streams microphone audio to the Mac. The Mac writes it into a
  virtual microphone (for example BlackHole 2ch) and holds the dictation hotkey of
  the user's chosen tool (default: hold `Fn`), so the tool types the text.
- **UP** and **DOWN** report gestures; the Mac maps them to user-defined actions.

## Roles and transport

| Item | Value |
| --- | --- |
| Device role | BLE peripheral, connectable advertising, one connection |
| Mac role | BLE central (CoreBluetooth) |
| Advertising name | `Passport-XXXX` (last two bytes of the BT MAC, upper-case hex) |
| Service | Nordic UART Service `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| Mac to device | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` (RX): `WRITE`, `WRITE_NO_RSP`, encryption required |
| Device to Mac | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` (TX): `NOTIFY`, encryption required |
| Preferred ATT MTU | 247 |
| Security | LE Secure Connections, MITM, bonding; the device displays a 6-digit passkey (≥ 100000) |

The link is ready only when it is connected, encrypted, subscribed, and both hello
messages have been exchanged.

## Frame format

```text
[0] 0xA5  [1] 0x5A  [2] TYPE  [3] FLAGS  [4..5] LEN (big endian)  [6..] payload
```

Receivers scan byte by byte for the magic plus a known type plus a legal length,
and discard a partial frame when a new valid header appears inside it.

| Type | Name | Direction | Payload | Max payload |
| --- | --- | --- | --- | --- |
| `0x03` | `CONTROL` | Mac → device | UTF-8 JSON object | 512 |
| `0x04` | `EVENT` | device → Mac | UTF-8 JSON object | 512 |
| `0x05` | `AUDIO_OPUS` | device → Mac | `[SEQ:1]` + one Opus packet | 1 + 512 |
| `0x07` | `PUT_DATA` | Mac → device | `[OFFSET:4 BE]` + bytes of the asset being uploaded | 4 + 224 |

Flags (`0x01 MORE`, `0x02 FIRST`, `0x04 LAST`) are reserved; all frames defined
here are single-frame messages with flags `0`.

## Handshake

Device, as an `EVENT` after subscription:

```json
{"ev":"hello","proto":1,"fw":"0.1.0","model":"Passport-3A2B",
 "caps":["opus","put","display"],
 "page":{"w":240,"h":320,"fmt":"rgb565le","count":2},
 "battery":87,"display":"auto"}
```

Mac, as a `CONTROL` once it has seen the device hello:

```json
{"cmd":"hello","proto":1,"app":"BadgeMic","ver":"0.1.0"}
```

`battery` is a percentage, or `-1` when unavailable. A peer that sees a different
`proto` must show a visible version-mismatch message and must not stream audio.

## Push-to-talk

Audio: Opus, 16 kHz, mono, 60 ms frames (960 samples), `OPUS_APPLICATION_VOIP`,
complexity 0, DTX on. `SEQ` increments per audio frame and wraps at 256; it is
used only for loss accounting. Audio is never retransmitted.

Sequence:

1. OK goes down. The device starts capturing immediately into a pre-roll buffer.
2. If OK is released before **300 ms**, it is a tap: no events are sent, the
   buffered audio is discarded, and the device switches to the next badge page.
3. When OK has been held for 300 ms, the device sends
   `{"ev":"ptt_start","id":N}`, then the buffered frames, then live frames.
4. On release the device sends the last frame, then
   `{"ev":"ptt_end","id":N,"frames":F,"dropped":D}`.
5. If the link drops or the hold exceeds **120 s**, the device ends the turn with
   `{"ev":"ptt_end","id":N,"frames":F,"dropped":D,"reason":"timeout"|"link"}`.

`id` increments per turn. The Mac must release the dictation hotkey on
`ptt_end`, on disconnect, and when no audio or event has arrived for 2 s during a
turn, so a key can never stay stuck down.

The Mac may answer `{"cmd":"ptt_ack","id":N}` once the hotkey is held and the
virtual microphone is receiving; the device uses it only to change the recording
indicator from "capturing" to "live".

## Keys

```json
{"ev":"key","key":"up","gesture":"click"}
```

`key` is `up` or `down`; `gesture` is `click` or `long` (held ≥ 600 ms, reported
on release). OK never produces a `key` event. When the screen is off, the first
UP or DOWN press only wakes the screen and is not reported. OK hold works while
the screen is off and wakes it.

## Display mode

```json
{"cmd":"set_display","mode":"auto"}
```

`auto`: dim after 30 s idle, screen off after 120 s (the BLE link stays up, so OK
works instantly), deep sleep after 30 min idle. `always_on`: the screen never
turns off; the device leaves `always_on` by itself when the battery drops below
15 %. The device persists the mode and reports changes as
`{"ev":"display","mode":"always_on"}`.

## Badge page upload

Each page is exactly `w × h × 2` bytes (153 600 for 240 × 320) of RGB565,
little-endian, row-major, no header. Page `0` is shown at boot; an OK tap cycles
through the pages that hold a valid image.

1. Mac: `{"cmd":"put_begin","page":0,"size":153600,"crc32":3735928559}`
   (CRC-32/ISO-HDLC, the zlib `crc32`).
2. Device prepares an inactive slot and answers `{"ev":"put_ready","page":0}`,
   or `{"ev":"put_done","page":0,"ok":false,"err":"..."}`.
3. Mac sends `PUT_DATA` frames in offset order using write-with-response, at most
   224 data bytes each.
4. Mac: `{"cmd":"put_end","page":0}`.
5. Device verifies size and CRC, makes the slot active only on success, redraws if
   that page is visible, and answers `{"ev":"put_done","page":0,"ok":true}`.

`{"cmd":"clear_page","page":1}` removes a page. A failed or interrupted upload
never replaces the previously active image.

## Status

The device sends `{"ev":"battery","battery":86}` when the reported percentage
changes, at most once per minute.

## Error handling

Unknown commands are answered with `{"ev":"error","cmd":"<name>","err":"unknown"}`.
Malformed JSON is logged and ignored. Frames of unknown type are treated as
misalignment and skipped by the reassembler.
