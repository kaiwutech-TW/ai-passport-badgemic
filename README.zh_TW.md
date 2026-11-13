<p align="right"><a href="README.md">English</a> · <strong>繁體中文</strong> · <a href="README.zh_CN.md">简体中文</a></p>

# AI Passport Badge Mic

**網站：[badgemic.kaiwu.com.tw](https://badgemic.kaiwu.com.tw)**

把 [FoloToy AI Passport](https://github.com/FoloToy/ai-passport) 變成兩樣可以戴在身上的東西：

- **名牌**：直式名片頁加一個 QR 碼（你的聯絡人 vCard，或任何連結），在 Mac 上設計好再傳上去。
- **Mac 聽寫用的無線按鍵麥克風**：按住 OK 說話，聲音經藍牙送到配套 App **BadgeMic**。App 會把聲音播進虛擬麥克風，
  同時按住你的聽寫工具快捷鍵。放開按鈕，文字就打在你正在輸入的地方。

只要聽寫工具有快捷鍵、能選麥克風就能用，例如學舌鳥（Parroting）、豆包、macOS 內建聽寫。
除非你的聽寫工具自己上傳，聲音不會送到任何雲端服務。

> 這是以 FoloToy 開源韌體範本為基礎的社群專案，不是 FoloToy 官方產品，也不由 FoloToy 提供支援。

## 需要準備

- FoloToy AI Passport（ESP32-C3）和一條可傳資料的 USB-C 線
- Apple 晶片的 Mac，macOS 14 以上
- 一個有「按住說話」或「按一下開始/結束」快捷鍵的聽寫工具

## 快速開始

### 1. 先備份，再刷名牌韌體

刷機會取代出廠韌體。請先完整備份 Flash，以後才能還原：

```sh
pip install esptool
# 找序列埠：ls /dev/cu.usbmodem*
esptool --chip esp32c3 -p /dev/cu.usbmodemXXXX read_flash 0 0x800000 passport-backup.bin
```

備份檔含有裝置身分，請自己保管、不要分享。接著從 [Releases](https://github.com/kaiwutech-TW/ai-passport-badgemic/releases) 下載韌體並刷入：

```sh
esptool --chip esp32c3 -p /dev/cu.usbmodemXXXX -b 460800 write_flash 0x0 badgemic-passport-firmware-<版本>.bin
```

- **不要**執行 `erase_flash`，它會一併刪掉裝置身分和永久 Recovery 分區。
- 想回到官方韌體：把備份寫回 `0x0`，或照 FoloToy 的[恢復出廠韌體教學](https://ai-passport.folotoy.cn/guides/restore-factory-firmware/)操作。

### 2. 安裝 Mac App

1. 從 [Releases](https://github.com/kaiwutech-TW/ai-passport-badgemic/releases) 下載 `BadgeMic-<版本>.dmg`，把 **BadgeMic** 拖進「應用程式」。
   App 已簽名並通過 Apple 認證，可以直接打開。
2. 打開 BadgeMic（在選單列），到 **「聽寫」** 分頁：
   - 按 **「安裝虛擬麥克風」** 並輸入密碼，系統會多一個音訊裝置 **BadgeMic Microphone**。安裝時 Mac 的聲音會中斷一兩秒。
   - 按 **「請求授權」**，到「系統設定 → 隱私權與安全性 → 輔助使用」允許 BadgeMic。
     App 需要這個權限才能幫你按聽寫快捷鍵。授權後請結束並重開 BadgeMic。
3. 配對名牌：BadgeMic 會自動找到名牌，macOS 要求配對碼時，輸入名牌螢幕上的 6 位數。

### 3. 設定聽寫工具

1. 在 BadgeMic 的「聽寫」分頁選觸發設定檔（學舌鳥、豆包或自訂），設成你的工具用的快捷鍵。
   按住才收音的工具選 **「按住」**，按一下開始、再按一下結束的工具選 **「切換」**。
2. 到 **聽寫工具自己的設定** 裡，把麥克風選成 **BadgeMic Microphone**。很多工具有自己的麥克風設定，不跟系統設定。
3. 按住名牌的 OK 說一句話再放開，文字應該就會出現。

### 4. 設計名牌

在 **「名牌」** 分頁填姓名、職稱、公司、電話、Email、網站，可加照片，然後上傳。
名牌有兩頁：名片頁和 QR 碼頁，輕按 OK 切換。

## 使用方式

| 按鍵 | 動作 |
| --- | --- |
| OK 輕按 | 切到下一頁名牌 |
| OK 按住 | 說話：名牌顯示麥克風與音量條，放開就送出 |
| UP / DOWN 短按或長按 | 在 **「按鍵」** 分頁自訂：快捷鍵、Return、Escape、文字片段、「捷徑」、開啟 App，或（長按）關閉名牌螢幕 |

- **螢幕**：在 **「裝置」** 分頁設定多久調暗、多久熄屏、亮度，或設成常亮。按任一鍵會喚醒，第一下只負責喚醒。
- **太大聲？** 聲音爆音時音量條會變紅，到「裝置」分頁調低 **「收音增益」**。
- **NFC**：背面的 NFC 是獨立晶片，可以用手機 App（例如 NFC Tools）寫入任何連結，例如 LINE 加好友網址或 LinkedIn。
  寫之前先讀取、保存原本的內容，而且千萬不要鎖定標籤。

## 從原始碼建置

- 韌體：ESP-IDF 5.5。先讀 [AGENTS.md](AGENTS.md)，再執行 `./tools/validate.sh`（主機測試 + 韌體建置）。
- Mac App 與虛擬麥克風驅動：見 [companion/macos/README.zh_CN.md](companion/macos/README.zh_CN.md)，只需要 Xcode Command Line Tools。
- 名牌 ↔ Mac 通訊協定：[docs/development/engineering/badge-mic-protocol.zh_CN.md](docs/development/engineering/badge-mic-protocol.zh_CN.md)。
- FoloToy 原專案說明：[docs/README.zh_CN.md](docs/README.zh_CN.md)。

## 致謝與授權

MIT，見 [LICENSE](LICENSE)。基於 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport)（MIT）。
藍牙序列通道改寫自 Shinku-Chen 的 [Pocket Intercom](https://github.com/Shinku-Chen/ai-passport)（MIT）。「透過虛擬麥克風驅動聽寫工具」的想法來自
[SayAll](https://github.com/HD838A/remote-mic-app)；BadgeMic 與其虛擬麥克風驅動都是從零撰寫，不含其程式碼。
