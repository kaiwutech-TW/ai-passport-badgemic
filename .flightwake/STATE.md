---
updated: 2026-11-13
updated_by: Claude Code (claude-opus-5-5)
latest_record: records/261113-scope-and-toolchain.md
health: yellow  # 端到端已在真機打通;本輪韌體/驅動/簽名改動都還沒上真機
---
<!-- flightwake STATE — 永遠短、永遠新。新 session 的第一站。 -->
<!-- 規則:只寫「現在」與「下一步」;歷史去 records/,決策去 DECISIONS.md。 -->
<!-- 冷啟動契約:讀完本檔 + latest_record 必須能在 5 分鐘內安全接手。 -->

# 現在在哪

替 FoloToy AI Passport(ESP32-C3、8 MB Flash、無 PSRAM、只有 BLE)做韌體 + Mac App「BadgeMic」:直式名牌 + 按住 OK 說話,
聲音經 BLE 進 Mac 虛擬麥克風並按住辨識工具快捷鍵(預設 Fn)。設計見 DECISIONS 2026-11-13,協定見
`docs/development/engineering/badge-mic-protocol.md`(雙方合約)。開發分支 `feature/badge-mic`。

# 進行中(未完成勿刪)

- [x] 2026-11-13 真機端到端打通(韌體 fda4d3b + MiRemoteV 2ch):學舌鳥打出字;always_on 使用者測過 OK
- [ ] 韌體 649246e(validate PASS,封存 `build/firmware/ca8643ca…`)**未刷**:編碼堆疊 32 KB、優先序 6、每輪編碼統計、`off_key` 長按關螢幕。
      刷法:只寫 app `FoloToy-AI-Passport.bin` 到 0x10000(保留配對);刷前要使用者同意
- [ ] 自寫虛擬麥克風驅動 `companion/macos/Driver/`(主機端 harness 760 checks PASS)**未安裝**:裝要 sudo + 重啟 coreaudiod(全機聲音短暫中斷),要使用者同意/自己輸入密碼
- [ ] App 改用 Apple Development 憑證簽名(DR 不再是 cdhash)→ 第一次仍需重新授權輔助使用一次,之後重編應不再掉(待驗證;驗過就把 TRAPS adhoc 條目改 superseded)
- [ ] 未測:名牌 vCard(電話/Email/網站)上傳、UP/DOWN 動作、off_key、深睡喚醒、熄屏後 USB 序列埠消失(IDF 在 USB 連線時會保留 PLL,降頻不太像原因)
- [ ] 轉 MacBook Air:Apple Development 簽名在別台仍要右鍵打開;要免右鍵需 Developer ID Application 憑證 + notarize(使用者有開發者帳號,尚未建該憑證)

# 下一步入口

1. 使用者同意後:刷 649246e(app-only)→ 有界觀察開機 log → 按住說話看 `enc_first/enc_avg/enc_max/stack_free`
2. 使用者同意後:`companion/macos/Driver/install.sh` → 系統設定看到「BadgeMic Microphone」→ App 輸出選它、學舌鳥麥克風選它
3. 收尾補 record(自 02de2fb 起已多個 commit)
4. 刷機備份在 `~/esp/passport-backup/factory-full-20261113.bin`(含裝置序號與金鑰,勿 commit/上傳)

# 常備事實(這個 repo 的 3-5 條保命知識)

- 啟用工具鏈:`export PATH="$(brew --prefix python@3.12)/libexec/bin:$PATH" && . ~/esp/esp-idf-v5.5.3/export.sh`(否則找不到 venv,見 TRAPS)
- 完成關卡:`./tools/validate.sh`(static + firmware);Mac:`companion/macos/scripts/test.sh`;驅動:`companion/macos/Driver/tests/run.sh`
- 絕不 `erase-flash`;已配對的裝置只寫 app 區 0x10000,整片 merged 到 0x0 會清掉 NVS 配對
- 本機 `main` 只當上游基線(追蹤 `upstream/main`),開發走 `feature/*`
