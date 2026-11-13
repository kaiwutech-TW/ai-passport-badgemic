---
updated: 2026-11-13
updated_by: Claude Code (claude-opus-5-5)
latest_record: records/261113-badge-mic-bringup.md
health: yellow  # 真機端到端(含自寫驅動)已通;新韌體編碼統計未讀到、USB 斷線根因未明
---
<!-- flightwake STATE — 永遠短、永遠新。新 session 的第一站。 -->
<!-- 規則:只寫「現在」與「下一步」;歷史去 records/,決策去 DECISIONS.md。 -->
<!-- 冷啟動契約:讀完本檔 + latest_record 必須能在 5 分鐘內安全接手。 -->

# 現在在哪

替 FoloToy AI Passport(ESP32-C3、8 MB Flash、無 PSRAM、只有 BLE)做韌體 + Mac App「BadgeMic」:直式名牌 + 按住 OK 說話,
聲音經 BLE 進 Mac 虛擬麥克風並按住辨識工具快捷鍵(預設 Fn)。設計見 DECISIONS 2026-11-13,協定見
`docs/development/engineering/badge-mic-protocol.md`(雙方合約)。開發分支 `feature/badge-mic`。

# 進行中(未完成勿刪)

- [x] 2026-11-13 真機端到端(韌體 649246e + 自寫驅動 BadgeMic Microphone + 學舌鳥)使用者確認可用;always_on 測過 OK
- [ ] 新韌體每輪編碼統計(enc_first/avg/max、stack_free)尚未讀到:USB 序列埠開機約 210 秒後消失(根因未明,只影響 log)
      → 考慮把統計放進 `ptt_end` 事件讓 Mac App 記錄
- [ ] 真機語音峰值到 0 dBFS(削波):降增益(韌體 ES8311 增益或 App 端)
- [ ] App 已用 Apple Development 憑證簽名:重編後輔助使用授權是否保留待驗證(驗過把 TRAPS adhoc 條目改 superseded)
- [ ] 未測:名牌 vCard(電話/Email/網站)上傳、UP/DOWN 動作、off_key 長按關螢幕、深睡喚醒
- [ ] 轉 MacBook Air:要免右鍵開啟需 Developer ID Application 憑證 + notarize(使用者有開發者帳號,未建);驅動安裝需管理員密碼
      (`osascript … with administrator privileges` 跑 `companion/macos/Driver/install.sh`;`!` 前綴的 shell 沒有 tty,sudo 會失敗);
      辨識工具要在它自己的設定裡選 BadgeMic Microphone(TRAPS parroting-own-mic-setting)

# 下一步入口

1. 測 off_key、vCard 名牌上傳、UP/DOWN(都不用刷機)
2. 韌體:編碼統計進 `ptt_end`、降增益 → validate → 告知使用者後只刷 0x10000
3. 刷機備份在 `~/esp/passport-backup/factory-full-20261113.bin`(含裝置序號與金鑰,勿 commit/上傳)

# 常備事實(這個 repo 的 3-5 條保命知識)

- 啟用工具鏈:`export PATH="$(brew --prefix python@3.12)/libexec/bin:$PATH" && . ~/esp/esp-idf-v5.5.3/export.sh`(否則找不到 venv,見 TRAPS)
- 完成關卡:`./tools/validate.sh`(static + firmware);Mac:`companion/macos/scripts/test.sh`;驅動:`companion/macos/Driver/tests/run.sh`
- 絕不 `erase-flash`;已配對的裝置只寫 app 區 0x10000,整片 merged 到 0x0 會清掉 NVS 配對
- 本機 `main` 只當上游基線(追蹤 `upstream/main`),開發走 `feature/*`
