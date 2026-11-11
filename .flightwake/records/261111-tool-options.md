---
record_id: 261111-tool-options
session: Codex
date: 2026-11-11
repos: [ai-passport]
tests: 無 runtime 面；僅文件調查，未編譯、未跑 typecheck、未驗證真機
prod_changes: none
---

# AI Passport 實用工具方向與現成專案調查

**TL;DR**: 使用者詢問 ESP32-C3、8 MB Flash、無 PSRAM 的官方 AI Passport 適合做哪些工具。已查官方硬體資料及應用原始文件，建議優先考慮語音筆記／待辦或 AI 工作狀態看板；這是候選建議，尚未選型或開始實作。

## 關鍵發現

1. 官方規格：240×320 TFT、三顆功能鍵、ES8311 麥克風／喇叭、500 mAh 電池、Wi-Fi 及 BLE；NTAG213 是被動 NFC 標籤。規格來源：https://github.com/FoloToy/ai-passport/blob/main/docs/hardware-design/specifications.md 。
2. 語音筆記已有 Xiaonuo Jianlu：裝置錄音與離線佇列，外部 hub 轉文字及分類。官方收錄的社群應用文件：https://github.com/FoloToy/ai-passport/blob/main/docs/reference/noah-1106/xiaonuo-jianlu/README.md 。不是本 session 的真機驗證。
3. 官方 Claude Buddy 分支已有 BLE 狀態、近期訊息及審批 UI，但文件明說 Hardware Buddy 為實驗介面，板級驗收清單未勾選。來源：https://github.com/FoloToy/ai-passport/tree/demo/claude-buddy-port 。不能推論可直接接其他 coding agent。
4. 官方飛書韌體已有訊息清單及語音轉文字回覆：https://github.com/FoloToy/ai-passport-feishu 。小智上游也有此板定義：https://github.com/78/xiaozhi-esp32/blob/main/main/boards/folotoy/ai-passport/README.md ；按鍵語音有參考價值，但目前版本部分省電驗收仍待補。
5. Pocket Intercom 有 Android 中介與 BLE 音訊路線，不能推定支援 iOS。其文件新版 TTS 描述與舊驗收段互相矛盾；選用前須查對應版本源碼及實測：https://github.com/FoloToy/ai-passport/blob/main/docs/reference/shinku-chen/intercom/README.md 。
6. 原生 USB Serial/JTAG 為固定功能，不能直接改成標準 USB 鍵盤或音效卡：https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-guides/usb-serial-jtag-console.html 。

## 交付 / Commits

研究前 HEAD：6cf7fbd。此筆紀錄與 STATE 在本次收尾 commit 一起保存；未改應用程式或刷機。

## 驗證證據

- 2026-11-11 透過網路讀取上述官方／作者文件；現成專案的功能宣稱與本地實測明確區分。
- 本地只有初始空 commit 與未追蹤的框架設定，沒有可執行的應用、測試或 typecheck。
- 公開來源連結已人工檢視，沒有裝置身份網址或憑證。

## 未完 / 交接

- 調查完成；用途、手機平台及連線方式尚待使用者選定，下一步見 STATE。沒有跨 session 的實作建設。
