<!-- flightwake TRAPS — 坑 registry。非顯而易見、會再咬人的事實。 -->
<!-- 條目採 OKF 式慣例:frontmatter 區塊 + 內文;可用 [[名稱]] 互連。新的加最上面。 -->
<!-- 時效:條目過時(功能合併/重構後不再成立)不刪 — status 改 superseded 並指向取代者;讀的人只信 active。 -->
<!-- 證據強度:confidence 標的是「根因」的把握度,不是症狀。**不對稱門檻**——拿這條論證
     「這樣會壞」probable 就夠(錯了只是多做防護);論證「這樣是安全的」必須 confirmed
     (錯了直接打到 prod 和使用者)。 -->

# 坑 Registry

---
name: parroting-own-mic-setting
type: gotcha
status: active
confidence: confirmed  # 改學舌鳥自身麥克風設定前後一敗一成;同時段 ffmpeg 證實聲音已進驅動(2026-11-13)
tags: [macos, dictation, parroting, virtual-mic, companion]
discovered: 2026-11-13
paths: ["companion/macos/Sources/BadgeMic/DictationTab.swift"]
---

**症狀**:BadgeMic 輸出設為 BadgeMic Microphone、系統設定輸入也選它,按住說話學舌鳥卻收不到;改回 MiRemoteV 就正常。
**根因**:學舌鳥(Parroting)有自己的麥克風設定(`~/Library/Application Support/com.parroting.mac/settings_store.json` 的
`settings.selected_microphone`),不跟系統預設輸入。它還停在舊的 MiRemoteV 2ch。
**解法**:在學舌鳥自己的設定裡把麥克風改成 BadgeMic Microphone。其他辨識工具(豆包等)也要各自檢查是否有獨立的麥克風選項。
**佐證**:record 261113-badge-mic-bringup 關鍵發現 1。

---
name: adhoc-sign-resets-accessibility
type: gotcha
status: active
confidence: probable  # 1 次實機觀察 + codesign 顯示 DR 為 cdhash;未做「固定憑證後不再失效」的對照
tags: [macos, tcc, codesign, companion]
discovered: 2026-11-13
paths: ["companion/macos/scripts/build-app.sh"]
commands: ["./scripts/build-app.sh", "companion/macos/scripts/build-app.sh"]
retire_when: "build-app.sh 改用固定的自簽或 Developer ID 憑證簽名時"
---

**症狀**:系統設定 → 隱私權與安全性 → 輔助使用裡 BadgeMic 開關是開的,但 App 重開後仍顯示「輔助使用:未授權」,模擬的 Fn 完全無效(macOS 靜默丟棄事件)。
**根因**:`build-app.sh` 用 ad-hoc 簽章,`codesign -d -r-` 顯示 designated requirement 是 `cdhash H"…"`;每次重編 cdhash 都變,TCC 的舊授權綁舊 cdhash,新版不吃。
**繞法**:每次重編後 `tccutil reset Accessibility tw.kaiwu.badgemic`(只影響本 App),重開 App → 請求授權 → 系統設定打開 → 再重開 App。
長期解:用固定憑證簽名,讓 DR 不隨重編改變。
**佐證**:2026-11-13 本 session,`fda4d3b` 後重編 App 出現此症狀;codesign 輸出 `Signature=adhoc`、`designated => cdhash H"c8f8…"`。

---
name: idf-export-wrong-python
type: gotcha
status: active
confidence: confirmed  # 同一 shell 內改 PATH 前後一敗一成(2026-11-13)
tags: [esp-idf, python, toolchain, macos]
discovered: 2026-11-13
commands: [". ~/esp/esp-idf-v5.5.3/export.sh", "source ~/esp/esp-idf-v5.5.3/export.sh"]
retire_when: "改用與 install 時相同的系統 python 重裝 IDF,或 repo 有包好 PATH 的啟用腳本時"
---

**症狀**:`. ~/esp/esp-idf-v5.5.3/export.sh` 失敗:
`ERROR: ESP-IDF Python virtual environment "/Users/kaiwu/.espressif/python_env/idf5.5_py3.9_env/bin/python" not found.`,之後 `idf.py: command not found`。
**根因**:export.sh 依 PATH 上第一個 `python3` 的版本去找 `idf5.5_py<版本>_env`;本機 install.sh 是用 brew `python@3.12` 跑的(venv 為 `idf5.5_py3.12_env`),
但系統 PATH 預設的 `python3` 是 `/usr/bin/python3`(3.9.6)。
**解法**:每次啟用前先把 3.12 放到 PATH 前面:
`export PATH="$(brew --prefix python@3.12)/libexec/bin:$PATH" && . ~/esp/esp-idf-v5.5.3/export.sh`,然後 `idf.py --version` 必須印出 `ESP-IDF v5.5.3`。
**佐證**:2026-11-13 本 session:未改 PATH → 上述錯誤;改 PATH 後同一指令 → `ESP-IDF v5.5.3`、`Python 3.12.16`。

---
name: erase-flash-wipes-device-identity
type: constraint
status: active
confidence: probable  # 兩份一手文件一致,未在真機實驗
tags: [flash, esptool, recovery, factory-firmware]
discovered: 2026-11-11
commands: ["idf.py erase-flash", "idf.py erase_flash", "esptool.py erase_flash", "esptool erase_flash", "esptool.py write_flash"]
retire_when: "在本機真機上驗證過 erase/刷機後 cardid 與 Recovery 的實際行為"
---

**症狀**:尚未踩到(預防性登記)。上游文件警告:執行 `erase-flash` 會清掉設備身份與永久 Recovery。
**根因**:出廠韌體在 flash 固定位址放了 `cardid` 設備身份(`0x356000`)與永久 Recovery(`0x700000`,開機按住上鍵 5 秒進入),
整片 erase 會一併抹掉;社群韌體刷入也會覆蓋原本內容,出廠「身份卡」要先恢復預設韌體才能再用微信小程序同步。
**繞法**:不要 `erase-flash`;只刷 `build/FoloToy-AI-Passport-full.bin` 合併鏡像;已有設備身份的機器優先走小程序 Recovery 安裝。
刷前若想保留出廠韌體,先用 `esptool.py read_flash 0 0x800000 backup.bin` 整片備份(此步本身未驗證)。
官方恢復路徑:手機掃機背 QR → 裝置專屬頁(連結含 SN 與 KEY,屬機密,不可 commit/分享)→ 桌面 Chrome/Edge + 資料線
→「恢復默認固件」→「執行完整恢復」→ 小程序重新同步(https://ai-passport.folotoy.cn/guides/restore-factory-firmware/)。
erase 掉 cardid 後這條路還通不通,文件沒寫、未驗證。
**佐證**:FoloToy/ai-passport `feature/senior-safety-card` 分支 README「構建與安裝」「兼容性」節;
官網 https://ai-passport.folotoy.cn/guides/getting-started/(「如果已安裝過其他玩法，請先恢復默認固件」)。

---
name: {{kebab-case-slug}}
type: trap          # trap | gotcha | constraint
status: active      # active | superseded(過時不刪,改此欄並在內文指向 [[取代條目]] 或 record)
confidence: suspected  # confirmed(受控實驗坐實:改變因 → 症狀跟著出現/消失,至少兩次;
                       #           非因果事實則為「窮盡查證 + 指得出一手來源」)
                       # probable(多次觀察一致,但沒做對照組)
                       # suspected(單次觀察,或「當下最像的解釋」)
                       # 未標此欄 = unknown(舊條目),讀的人比照 suspected 對待
tags: [{{標籤}}]
discovered: {{YYYY-MM-DD}}
paths: []          # 選填:這個坑相關的檔案,repo 相對路徑的 glob — 例如 ["src/db/**", "*.sql"]
commands: []       # 選填:這個坑相關的指令前綴 — 例如 ["npm run migrate"]。留空即可;
                   #   裝了 Claude Code mod 時,碰到相符的檔案或指令會把這條提示給 agent(每 session 一次)
---

**症狀**:{{看到什麼(錯誤訊息/怪行為)——這欄永遠是事實,錯誤訊息照貼}}
**根因**:{{一句話。confidence 標的就是這一欄的把握度}}
**解法/繞法**:{{怎麼處理。非 confirmed 時只寫「繞法」,不寫「解法」,也不得當行為準則}}
**佐證**:{{commit/record 連結;標 confirmed 必須指得出受控實驗}}
