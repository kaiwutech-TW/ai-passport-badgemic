---
updated: 2026-11-13
updated_by: Claude Code (claude-opus-5-5)
latest_record: records/261113-scope-and-toolchain.md
health: yellow  # 端到端已在真機打通;仍有已知風險未修(編碼任務堆疊只剩 472 B)
---
<!-- flightwake STATE — 永遠短、永遠新。新 session 的第一站。 -->
<!-- 規則:只寫「現在」與「下一步」;歷史去 records/,決策去 DECISIONS.md。 -->
<!-- 冷啟動契約:讀完本檔 + latest_record 必須能在 5 分鐘內安全接手。 -->

# 現在在哪

替 FoloToy AI Passport(ESP32-C3、8 MB Flash、無 PSRAM、只有 BLE)做韌體 + Mac App「BadgeMic」:直式名牌 + 按住 OK 說話,
聲音經 BLE 進 Mac 虛擬麥克風並按住辨識工具快捷鍵(預設 Fn)。設計見 DECISIONS 2026-11-13,協定見
`docs/development/engineering/badge-mic-protocol.md`(雙方合約)。開發分支 `feature/badge-mic`。

# 進行中(未完成勿刪)

- [x] 2026-11-13 真機端到端打通(韌體 fda4d3b):按住 OK → BLE Opus → BadgeMic → MiRemoteV 2ch 虛擬麥克風 + 模擬 Fn → 學舌鳥打出字(使用者確認)。
      裝置日誌:第 1 輪 109 幀、dropped=0、tx_drops=0;藍牙啟動後 heap 68 KB、首次 PTT 65 KB
- [ ] 韌體風險:編碼任務 stack_free=472 B(要加大)、enc_max=93 ms > 60 ms 幀長;熄屏後 USB 序列埠消失(疑為降頻,待確認)
- [ ] Mac App:「自動」輸出裝置只找 BlackHole,找不到就送喇叭 → 應認得 MiRemoteV 等 loopback 並提醒;ad-hoc 簽章每次重編掉輔助使用授權(TRAPS)→ 待使用者同意建固定自簽憑證
- [ ] 未測:名牌頁上傳、UP/DOWN 動作、顯示設定、always_on、深睡喚醒;豆包(使用者要在 MacBook Air 測)
- [ ] 使用者要求:用 K-Browser 開 Chrome 的 Google Flow 生成 App 圖示(吊牌 + 麥克風)

# 下一步入口

1. 修韌體堆疊/編碼時間 → validate → 只寫 app 區(0x10000)保留配對;刷前告知使用者
2. 刷機備份在 `~/esp/passport-backup/factory-full-20261113.bin`(含裝置序號與金鑰,勿 commit/上傳)
3. 轉到 MacBook Air 前:App 要右鍵打開放行、裝虛擬麥克風、重新授權;名牌需先在這台「忘記裝置」

# 常備事實(這個 repo 的 3-5 條保命知識)

- 啟用工具鏈:`export PATH="$(brew --prefix python@3.12)/libexec/bin:$PATH" && . ~/esp/esp-idf-v5.5.3/export.sh`(否則找不到 venv,見 TRAPS)
- 完成關卡:`./tools/validate.sh`(static + firmware);建置通過不等於真機驗證
- 絕不 `erase-flash`;刷機只寫驗證過的 `FoloToy-AI-Passport-full.bin` 到 `0x0`
- 本機 `main` 只當上游基線(追蹤 `upstream/main`),開發走 `feature/*`
