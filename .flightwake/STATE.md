---
updated: 2026-11-13
updated_by: Claude Code (claude-opus-5-5)
latest_record: records/261113-scope-and-toolchain.md
health: yellow  # 工具鏈與上游基線驗證全綠,但本專案應用程式碼尚未開始,無真機驗證
---
<!-- flightwake STATE — 永遠短、永遠新。新 session 的第一站。 -->
<!-- 規則:只寫「現在」與「下一步」;歷史去 records/,決策去 DECISIONS.md。 -->
<!-- 冷啟動契約:讀完本檔 + latest_record 必須能在 5 分鐘內安全接手。 -->

# 現在在哪

替 FoloToy AI Passport(ESP32-C3、8 MB Flash、無 PSRAM、只有 BLE)做一支韌體 + 一個 Mac 端 App:
平常是直式名牌;按住 OK 收音,聲音經 BLE 送到 Mac App,由使用者自選的辨識工具(學舌鳥/豆包,預設按住 Fn)打字。範圍與架構見 DECISIONS 2026-11-13。
repo 已對齊上游 FoloToy/ai-passport,開發分支 `feature/badge-mic`;ESP-IDF 5.5.3 已裝好,上游基線可編譯且驗證全綠。尚未寫應用程式碼。

# 進行中(未完成勿刪)

- [ ] 韌體:重新設計的名牌畫面 + OK 按住 → BLE 送 Opus(參考 Shinku-Chen intercom 的 `oc_link.c`/`oc_audio.c`,MIT)
- [ ] Mac App(Swift,MIT,自行重寫):BLE 收音 → Opus 解碼 → BlackHole 2ch → 觸發設定檔(快捷鍵 + 按住/切換 + 麥克風)
- [ ] 未驗證:模擬 Fn 能否觸發學舌鳥/豆包(使用者會在另一台 MacBook Air 試豆包)

# 下一步入口

1. 先讀 `AGENTS.md` 路由表 + `docs/development/ai-guide.md`(UI 必須重新設計);再看 intercom 原始碼的 BLE/Opus 部分 → 在 `main/` 開新應用
2. 刷機前必問使用者(裝置已用 USB 接上:`/dev/cu.usbmodem103101`);刷前提醒存好機背 QR 連結、會蓋掉小程序身份卡

# 常備事實(這個 repo 的 3-5 條保命知識)

- 啟用工具鏈:`export PATH="$(brew --prefix python@3.12)/libexec/bin:$PATH" && . ~/esp/esp-idf-v5.5.3/export.sh`(否則找不到 venv,見 TRAPS)
- 完成關卡:`./tools/validate.sh`(static + firmware);建置通過不等於真機驗證
- 絕不 `erase-flash`;刷機只寫驗證過的 `FoloToy-AI-Passport-full.bin` 到 `0x0`
- 本機 `main` 只當上游基線(追蹤 `upstream/main`),開發走 `feature/*`
