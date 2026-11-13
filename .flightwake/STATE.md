---
updated: 2026-11-13
updated_by: Claude Code (claude-opus-5-5)
latest_record: records/261113-badge-mic-bringup.md
health: green  # 真機端到端、自寫驅動、off_key、收音增益皆經使用者確認;剩編碼統計數字未讀、USB 斷線根因未明(只影響 log)
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
- [x] 2026-11-13 韌體 e5e9e7e 已刷(0x10000,配對保留)+ App fa4b123:使用者確認 off_key 長按 UP 關螢幕可用、
      24 dB 下要「超大聲且很近」才削波(預設維持 24)、「麥克風 → 收音增益」設定有出現
- [ ] 還沒看到 `ptt_end.stats` 的實際數字(enc_max、stack_free):請使用者截 App 記錄或「最近一輪」
- [ ] USB 序列埠開機約 210 秒後消失(根因未明,只影響 log)
- [ ] Mac 測試腳本偶發 `fatalError`(TestingMacros),重跑即過;`scripts/test.sh` 的重試沒接住這種輸出,待補
- [x] 2026-11-13 Developer ID Application 憑證已建(到期 2027-12-13,私鑰只在本機登入鑰匙圈)、notarytool 設定檔 `BadgeMic` 已存;
      `companion/macos/scripts/package.sh` 產出 `dist/BadgeMic-0.1.0.dmg`:App 與 DMG 皆 notarize Accepted + staple,spctl「Notarized Developer ID」
- [ ] 換成 Developer ID 簽名後,使用者需再授權輔助使用一次;之後重編是否保留待驗證(驗過把 TRAPS adhoc 條目改 superseded)
- [ ] 未測:名牌 vCard(電話/Email/網站)上傳、UP/DOWN 其他動作、深睡喚醒
- [ ] 轉 MacBook Air(未做):DMG 拖進「應用程式」→ 開啟 →「聽寫」分頁「安裝虛擬麥克風」(App 內建驅動,要管理員密碼)→ 授權輔助使用
      → 名牌在本機「忘記裝置」後到 Air 重新配對 → 辨識工具自己的麥克風設定選 BadgeMic Microphone(TRAPS parroting-own-mic-setting)。
      App 只有 arm64;Air 若是 Intel 要另做 x86_64

- [x] 2026-11-13 公開:fork https://github.com/kaiwutech-TW/ai-passport-badgemic(本機 remote `origin`,`feature/badge-mic` 推到 `main`);
      Release v0.1.0 附 DMG + 韌體(81da6a8)+ SHA256SUMS;三語 README;commit email 已改 GitHub noreply(改寫前備份在 `backup/pre-email-rewrite`)
- [ ] 待使用者決定:展示網頁(GitHub Pages)、投稿上游社群作品(PR 到 FoloToy `docs/reference/<username>/`)

# 下一步入口

1. 讀每輪統計(App 記錄),確認 enc_max < 60 ms、stack_free 有餘裕
2. 測 vCard 名牌上傳、UP/DOWN 其他動作
3. 搬到 MacBook Air(見上)
3. 刷機備份在 `~/esp/passport-backup/factory-full-20261113.bin`(含裝置序號與金鑰,勿 commit/上傳)

# 常備事實(這個 repo 的 3-5 條保命知識)

- 啟用工具鏈:`export PATH="$(brew --prefix python@3.12)/libexec/bin:$PATH" && . ~/esp/esp-idf-v5.5.3/export.sh`(否則找不到 venv,見 TRAPS)
- 完成關卡:`./tools/validate.sh`(static + firmware);Mac:`companion/macos/scripts/test.sh`;驅動:`companion/macos/Driver/tests/run.sh`
- 絕不 `erase-flash`;已配對的裝置只寫 app 區 0x10000,整片 merged 到 0x0 會清掉 NVS 配對
- 本機 `main` 只當上游基線(追蹤 `upstream/main`),開發走 `feature/*`
