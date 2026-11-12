---
updated: 2026-11-13
updated_by: Claude Code (claude-opus-5-5)
latest_record: records/261113-scope-and-toolchain.md
health: yellow  # Mac App 46 測試綠;韌體開發中(子代理,未 commit);無真機驗證
---
<!-- flightwake STATE — 永遠短、永遠新。新 session 的第一站。 -->
<!-- 規則:只寫「現在」與「下一步」;歷史去 records/,決策去 DECISIONS.md。 -->
<!-- 冷啟動契約:讀完本檔 + latest_record 必須能在 5 分鐘內安全接手。 -->

# 現在在哪

替 FoloToy AI Passport(ESP32-C3、8 MB Flash、無 PSRAM、只有 BLE)做韌體 + Mac App「BadgeMic」:直式名牌 + 按住 OK 說話,
聲音經 BLE 進 Mac 虛擬麥克風並按住辨識工具快捷鍵(預設 Fn)。設計見 DECISIONS 2026-11-13,協定見
`docs/development/engineering/badge-mic-protocol.md`(雙方合約)。開發分支 `feature/badge-mic`。

# 進行中(未完成勿刪)

- [x] 協定規格(b7dcd96、d713f8c:錄音後「已送出」0.8 s 回名牌;顯示時間/亮度可由 Mac 設定)
- [x] Mac App `companion/macos/`(5ee748f):`./scripts/test.sh` 46 測試通過;`scripts/build-app.sh` 產 `dist/BadgeMic.app`,尚未啟動過
- [ ] 韌體 `main/`、`partitions.csv`、`sdkconfig.defaults`:Sonnet 子代理開發中,工作樹有未 commit 變更——不要動、等它回報後由主線審查再 commit;
      已追加要求「PTT 期間每個 60 ms 幀都要送(含 DTX),否則 Mac 2 s 看門狗會放開 Fn」
- [ ] 未驗證:模擬 Fn 能否觸發學舌鳥/豆包;BlackHole 尚未安裝(需使用者輸密碼),目前只有 SayAll 的 MiRemoteV 2ch

# 下一步入口

1. 韌體回報後:審查 → `./tools/validate.sh` 全綠 → commit → 寫 record
2. 刷機前必問使用者(裝置在 `/dev/cu.usbmodem103101`);刷前提醒存好機背 QR 連結、會蓋掉小程序身份卡
3. 使用者自行處理 NFC:用 NFC Tools 先讀出原內容存證,再寫 LINE 加好友網址,勿鎖定標籤

# 常備事實(這個 repo 的 3-5 條保命知識)

- 啟用工具鏈:`export PATH="$(brew --prefix python@3.12)/libexec/bin:$PATH" && . ~/esp/esp-idf-v5.5.3/export.sh`(否則找不到 venv,見 TRAPS)
- 完成關卡:`./tools/validate.sh`(static + firmware);建置通過不等於真機驗證
- 絕不 `erase-flash`;刷機只寫驗證過的 `FoloToy-AI-Passport-full.bin` 到 `0x0`
- 本機 `main` 只當上游基線(追蹤 `upstream/main`),開發走 `feature/*`
