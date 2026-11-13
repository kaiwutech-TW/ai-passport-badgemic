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

Receivers scan byte by byte for the magic plus a known type plus a legal length.
For JSON frames (`CONTROL`, `EVENT`), a partial frame is discarded when a new valid
header appears inside it. Binary payloads (`PUT_DATA` pixels, `AUDIO_OPUS`) may
legally contain header-like bytes, so the device never applies that rule to
`PUT_DATA`; a receiver applying it to audio may lose at most that one frame.

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
 "caps":["opus","put","display","audio"],
 "page":{"w":240,"h":320,"fmt":"rgb565le","count":2},
 "battery":87,"battery_mv":4114,
 "display":{"mode":"auto","dim_s":30,"off_s":120,"sleep_min":30,"brightness":80,"dim_brightness":20,
            "off_key":"none"},
 "audio":{"mic_gain_db":24}}
```

Mac, as a `CONTROL` once it has seen the device hello:

```json
{"cmd":"hello","proto":1,"app":"BadgeMic","ver":"0.1.0"}
```

The device hello can arrive before the Mac's own subscription callback, or be lost.
If the Mac has no device hello 1 s after subscribing, it sends
`{"cmd":"get_hello"}` (retrying every 1.5 s, at most 8 times) and the device
answers with its hello. The device sends no `ptt_start`, audio, or `key` events until it has received a Mac
`hello` with a matching `proto`; a mismatch is answered with
`{"ev":"error","cmd":"hello","err":"proto"}`. `battery` is a percentage, or `-1`
when unavailable. `battery_mv` is the cell voltage in millivolts and is omitted
when unknown (older firmware never sends it). A peer that sees a different
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

After `ptt_end` the device shows a "sent" confirmation for 800 ms, then returns to
the badge page that was showing before the turn. If OK reaches 300 ms while the
link is not ready, the device shows "not connected" for 1.5 s, sends nothing, and
returns to the badge.

`ptt_end` also carries a `stats` object for diagnostics (all fields optional; the
Mac logs them and must ignore unknown ones):

```json
"stats":{"enc_first_us":41000,"enc_avg_us":18000,"enc_max_us":52000,
         "stack_free":7800,"read_err":0,"clip":3}
```

`enc_*_us` are Opus encode times per 60 ms frame (first frame, average, maximum),
`stack_free` is the encoder task's free stack in bytes, `read_err` counts failed
microphone reads, and `clip` counts frames whose peak reached full scale (≥ 32000).

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
UP or DOWN press, and an OK tap, only wake the screen and are not reported. OK hold works while
the screen is off and wakes it.

## Display mode

```json
{"cmd":"set_display","mode":"auto","dim_s":30,"off_s":120,"sleep_min":30,
 "brightness":80,"dim_brightness":20,"off_key":"none"}
```

Every field except `cmd` is optional; omitted fields keep their current value.

| Field | Default | Range | Meaning |
| --- | --- | --- | --- |
| `mode` | `auto` | `auto`, `always_on` | `always_on` never dims or turns the screen off |
| `dim_s` | 30 | 5–600 | idle seconds before dimming |
| `off_s` | 120 | `dim_s`–3600 | idle seconds before the screen turns off; the BLE link stays up, so OK works instantly |
| `sleep_min` | 30 | 0 (never) or 5–240 | idle minutes before deep sleep (link drops; a key press reboots) |
| `brightness` | 80 | 10–100 | normal backlight percent |
| `dim_brightness` | 20 | 5–`brightness` | dimmed backlight percent |
| `off_key` | `none` | `none`, `up_long`, `down_long` | long press (≥ 600 ms) of that key turns the screen off at once, handled on the device (works without the Mac); that gesture is then not sent as a `key` event |

Out-of-range values are clamped. The device persists the settings, includes them
in `hello` as `"display":{...}` with the same field names, and reports every change
as `{"ev":"display", ...same fields...}`. In `always_on` the device leaves that
mode by itself when the battery drops below 15 % and reports the change.

After an `off_key` press the screen stays off, also in `always_on`, until the next
key press (which only wakes the screen, as above) or a PTT turn / page upload. The
deep-sleep timer keeps counting from the last activity. An unknown `off_key` value
makes the whole `set_display` fail with `bad_args`.

## Microphone gain

```json
{"cmd":"set_audio","mic_gain_db":24}
```

`mic_gain_db` is the ES8311 microphone gain: 0–42 in 6 dB steps (other values are
rounded down to a step, out-of-range values clamped), default 24. The device
persists it, applies it from the next capture, includes it in `hello` as
`"audio":{"mic_gain_db":N}`, and reports every change as
`{"ev":"audio","mic_gain_db":N}`. A missing or non-integer `mic_gain_db` is `bad_args`.

During a turn the device's level meter turns red for 300 ms whenever a frame clips.

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

`{"cmd":"clear_page","page":1}` marks a page empty; it has no reply. A failed or interrupted upload
never replaces the previously active image.

## Status

The device sends `{"ev":"battery","battery":86,"battery_mv":3987}` when the
reported percentage changes or the voltage moves by 50 mV or more, at most once per
minute (`battery_mv` omitted when unknown).

The device does not trust the fuel gauge blindly: a reading of 0 % at 3.7 V or more,
or any reading below 2.5 V, is treated as a gauge glitch. The device keeps the last
good value (or reports `-1` if it has none), and after two such readings in a row
re-initializes the gauge, at most once every 10 minutes.

While the screen is off (`OFF` stage, including after `off_key`) and no turn or
upload is running, the device asks the Mac for a slow connection: 90–120 ms
interval, peripheral latency 4, 4 s supervision timeout. Any wake-up asks for
15–30 ms again. Commands sent by the Mac while the screen is off can therefore take
up to about 0.6 s to arrive.

## Error handling

Unknown commands are answered with `{"ev":"error","cmd":"<name>","err":"unknown"}`.
Malformed JSON is logged and ignored. Frames of unknown type are treated as
misalignment and skipped by the reassembler.
