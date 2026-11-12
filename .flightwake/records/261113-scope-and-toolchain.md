---
record_id: 261113-scope-and-toolchain
session: Claude Code (claude-opus-5-5)
date: 2026-11-13
repos: [ai-passport]
tests: validate.sh --static PASS(check_repo 416 檔 + host tests 26/18 OK);validate.sh --firmware PASS(上游基線,未含本專案程式);無真機驗證
prod_changes: none(未刷機、未開序列埠)
---

# 定案「名牌 + 按住說話麥克風」範圍,repo 接上上游、工具鏈可編譯

**TL;DR**:使用者把需求從「接 Gemini/ChatGPT 語音」收斂成兩個功能:直式名牌 + 像 SayAll 那樣按住 OK 收音、由 Mac 端自選的辨識工具(學舌鳥/豆包)打字。
本段把本機 repo 對齊上游 FoloToy/ai-passport、在 `feature/badge-mic` 開發,裝好 ESP-IDF 5.5.3 並讓上游驗證關卡全綠;還沒寫任何應用程式碼。

## 關鍵發現(重要性排序)

1. 訂閱帳號(ChatGPT Plus / Google AI Pro)目前沒有官方途徑給第三方裝置用;ESP32-C3 只有 BLE、沒有 Classic 藍牙,也當不了手機的藍牙耳機。→ 範圍決策見 DECISIONS 2026-11-13。
2. SayAll(HD838A/remote-mic-app,GPL-3.0)的機制:遙控器以 ATVV 經 BLE 傳音 → Mac App 寫進虛擬麥克風 → 模擬按住 Fn(CGEvent keycode 63 + `maskSecondaryFn`,source `hidSystemState`),辨識由輸入法/聽寫工具做。
   使用者要 MIT → 只參考做法自行重寫(DECISIONS)。學舌鳥(Parroting.app 0.4.19)設定為「fn / 按住 / Escape 取消」,使用者截圖確認。
3. 已有可參考的同硬體實作:社群「隨身 AI 對講機」(BLE NUS + Opus 16 kHz 上行約 3 KB/s,真機驗證;fork `Shinku-Chen/ai-passport` 分支 `feature/openclaw-intercom`,MIT,原始碼 `main/oc_link.c`、`main/oc_audio.c`);
   小智上游有 `main/boards/folotoy/ai-passport` 板定義(社群貢獻,非 FoloToy 官方);上游 `feature/senior-safety-card` 是離線展示卡,可參考名牌結構。
4. 容量:上游基線 app 1,524,992 bytes / factory 分區 8,323,072 bytes(約 18%);一次只能有一支韌體(官網:安裝新玩法覆蓋全部內容)。真正限制是內部 RAM(無 PSRAM)。
5. 上游 `tools/check_repo.py` 會把 flightwake 中文檔當成違規文件 → 改為 vendored 例外並把 CLAUDE.md 移到 `.claude/CLAUDE.md`(70b9bc8)。
6. 坑:ESP-IDF 啟用要先把 brew python@3.12 放 PATH 前面 → TRAPS `idf-export-wrong-python`(confirmed);刷機/erase 對出廠身份的影響 → TRAPS `erase-flash-wipes-device-identity`(probable)。

## 交付 / Commits

098ffc4..70b9bc8(flightwake 設定進 repo、決策、check_repo 例外)。本機舊 main 保留在 `archive/pre-upstream-main`;`main` 追蹤 `upstream/main`。
另:5 個上游必裝 skill 以 symlink 裝在 `.agents/skills/` 與 `.claude/skills/`(gitignored,`python3 tools/install_passport_skills.py --check` 可驗)。

## 驗證證據

- `idf.py --version` → `ESP-IDF v5.5.3`;IDF checkout `v5.5.3^{commit}` = HEAD = `2c211b2`,`git status` 乾淨。
- `./tools/validate.sh --firmware`(單獨執行)→ `Firmware build: PASS`,`Merged firmware: PASS (1590528 bytes, flash at 0x0)`。
- `./tools/validate.sh --static`(check_repo 改動後)→ `Repository checks: PASS (416 text files scanned)`、`Host tests: PASS`。
- 裝置以 USB 枚舉為 `USB JTAG_serial debug unit`(VID 0x303A / PID 0x1001),埠 `/dev/cu.usbmodem103101`;僅讀系統清單,未開埠。

## 未完 / 交接

- 模擬 Fn 能否觸發學舌鳥/豆包:測試腳本已寫好但未跑(使用者說不糾結,會在另一台 Mac 測豆包)。
- 尚未開始:韌體(名牌畫面 + OK 按住 BLE 送 Opus)與 Mac 端 App(收音 → BlackHole 虛擬麥克風 → 觸發設定檔)。下一步入口見 STATE。
