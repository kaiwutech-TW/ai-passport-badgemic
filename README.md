<p align="right"><strong>English</strong> · <a href="README.zh_TW.md">繁體中文</a> · <a href="README.zh_CN.md">简体中文</a></p>

# AI Passport Badge Mic

Turn a [FoloToy AI Passport](https://github.com/FoloToy/ai-passport) into two things you can wear:

- **A name badge.** A portrait name card and a QR code (your contact card as a vCard, or any link), designed on your Mac.
- **A wireless push-to-talk microphone for Mac dictation.** Hold the OK button and speak. The audio goes over Bluetooth
  to the companion app **BadgeMic**, which plays it into a virtual microphone and holds your dictation tool's hotkey.
  Release the button and the text appears wherever you are typing.

It works with any dictation tool that has a hotkey and lets you choose the microphone, for example Parroting,
Doubao or macOS Dictation. Nothing goes to a cloud service unless your dictation tool sends it there.

> This is a community project built on FoloToy's open-source firmware template. It is not made or supported by FoloToy.

## What you need

- A FoloToy AI Passport (ESP32-C3) and a USB-C data cable
- A Mac with Apple silicon running macOS 14 or later
- A dictation tool with a push-to-talk or toggle hotkey

## Quick start

### 1. Back up, then flash the badge firmware

Flashing replaces the factory firmware. Back up the whole flash first so you can go back later:

```sh
pip install esptool
# find the port: ls /dev/cu.usbmodem*
esptool --chip esp32c3 -p /dev/cu.usbmodemXXXX read_flash 0 0x800000 passport-backup.bin
```

Keep the backup private: it contains your device's identity. Then flash the firmware from the
[Releases](https://github.com/kaiwutech-TW/ai-passport-badgemic/releases) page:

```sh
esptool --chip esp32c3 -p /dev/cu.usbmodemXXXX -b 460800 write_flash 0x0 badgemic-passport-firmware-<version>.bin
```

- Do **not** run `erase_flash`. It also deletes the device identity and the permanent recovery partition.
- To go back to the official firmware, write your backup to `0x0`, or follow FoloToy's
  [restore guide](https://ai-passport.folotoy.cn/guides/restore-factory-firmware/).

### 2. Install the Mac app

1. Download `BadgeMic-<version>.dmg` from [Releases](https://github.com/kaiwutech-TW/ai-passport-badgemic/releases) and drag **BadgeMic** into Applications.
   The app is signed and notarized, so it opens normally.
2. Open BadgeMic (it lives in the menu bar). In the **Dictation (聽寫)** tab:
   - Click **Install virtual mic (安裝虛擬麥克風)** and enter your password. This adds an audio device called
     **BadgeMic Microphone**. Sound on the Mac pauses for a second while it installs.
   - Click **Request (請求授權)** and allow BadgeMic under *System Settings → Privacy & Security → Accessibility*.
     Accessibility lets the app press your dictation hotkey. Quit and reopen BadgeMic afterwards.
3. Pair the badge. BadgeMic finds it automatically. Enter the 6-digit code shown on the badge screen when macOS asks.

### 3. Set up your dictation tool

1. In BadgeMic's **Dictation** tab, pick a trigger profile (Parroting, Doubao, or Custom) and set the hotkey your
   tool uses. For tools that listen while a key is held, use **Hold**. For start/stop tools, use **Toggle**.
2. In **your dictation tool's own settings**, set the microphone to **BadgeMic Microphone**. Many tools keep their own
   microphone setting and ignore the system input device.
3. Hold OK on the badge, say something, release. The text should appear.

### 4. Design your badge

In the **Badge (名牌)** tab, enter your name, title, company, phone, email and website, optionally add a photo, and
upload. The badge has two pages: the name card and a QR code. Tap OK to switch between them.

## Using the badge

| Button | Action |
| --- | --- |
| OK, tap | Next badge page |
| OK, hold | Talk. The badge shows a microphone and a level meter. Release to send |
| UP / DOWN, short or long press | Whatever you choose in the **Buttons (按鍵)** tab: a shortcut, Return, Escape, a text snippet, a Shortcuts shortcut, open an app, or (long press) turn the badge screen off |

- **Screen**: in the **Device (裝置)** tab, set when the screen dims and turns off, the brightness, or keep it always on.
  Any button wakes it. The first press only wakes the screen.
- **Too loud?** The level meter turns red when your voice clips. Lower **Mic gain (收音增益)** in the Device tab.
- **NFC**: the tag on the back is a separate chip. Write any link to it (for example your LINE or LinkedIn profile)
  with a phone app such as NFC Tools. Read and save the original content first, and never lock the tag.

## Build from source

- Firmware: ESP-IDF 5.5. Start with [AGENTS.md](AGENTS.md), then run `./tools/validate.sh` (host tests and firmware build).
- Mac app and virtual microphone driver: see [companion/macos/README.md](companion/macos/README.md).
  Only the Xcode Command Line Tools are needed.
- Badge ↔ Mac protocol: [docs/development/engineering/badge-mic-protocol.md](docs/development/engineering/badge-mic-protocol.md).
- FoloToy's original project overview: [docs/README.md](docs/README.md).

## Credits and license

MIT, see [LICENSE](LICENSE). Built on [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport) (MIT).
The BLE serial link is adapted from Shinku-Chen's [Pocket Intercom](https://github.com/Shinku-Chen/ai-passport) (MIT). The idea of driving a dictation tool through a
virtual microphone comes from [SayAll](https://github.com/HD838A/remote-mic-app). BadgeMic and its virtual microphone
driver were written from scratch and contain no code from it.
