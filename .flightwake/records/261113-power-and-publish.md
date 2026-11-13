---
record_id: 261113-power-and-publish
session: Claude Code (claude-opus-5-5)
date: 2026-11-13
repos: [ai-passport-badgemic]
tests: validate.sh PASS(host tests + firmware build + layout);companion swift test 63 PASS
prod_changes: 展示頁部署到 Cloudflare Workers;FoloToy 社群市集投稿 1 件(審核中);真機刷 app 區 0x10000
---

# 展示頁上線、投稿社群市集，並修掉電量 0% 誤讀與熄屏耗電

**TL;DR**:先把 BadgeMic 的展示頁部署上線，並用官方 publisher 把作品投稿到 AI Passport 社群市集(狀態 pending,尚未公開)。
接著使用者回報掉電很快、電量顯示 0%:開機 log 證實電池其實 93%/4.11 V,0% 是電量計(CW2017)誤讀;掉電快主因是常亮模式。
新韌體加入電量讀數合理性檢查與電壓上報，熄屏時讓 LCD 進 Sleep In 並向 Mac 要求慢速 BLE 連線，已刷入真機並經使用者確認。

## 關鍵發現(重要性排序)

1. **0% 是電量計誤讀，不是沒電**:同一顆電池重開後立即讀到 SOC 93%、4114 mV(插 USB 充電中，電壓略偏高)。
   BSP 註解早已記載 CW2017 冷啟動會讀到 0/0;原本 App 層照單全收，還會因此觸發「低於 15% 退出常亮」。
   處置:`bm_battery_classify()` 把「0% 且 ≥3.7 V」或「<2.5 V」視為誤讀，保留上一個可信值;連續兩次就呼叫新的
   `bsp_battery_recover()` 重新接管電量計(每 10 分鐘最多一次，因為可能阻塞 5 s)。誤讀的真正觸發條件仍未知 → log 會留 `implausible battery reading`。
2. **熄屏後原本仍耗電的三項**:BLE 維持 Mac 預設的快速連線間隔、CPU 不能 light sleep(開機 log:`light sleep mode will not be able to apply when bluetooth is enabled`,
   板上沒有 32 kHz 晶振)、LCD 只關顯示沒進 Sleep In。這次處理前兩項以外的可做部分:LCD Sleep In(Sleep Out 前保證 120 ms)+ 熄屏時請求
   90–120 ms / latency 4 / timeout 4 s(符合 Apple 配件規範的參數組合),喚醒立刻請求 15–30 ms。實際省電幅度沒有量測。
3. **常亮模式不會自動熄屏也不進深睡**:這次的省電只在長按 UP(off_key)或自動模式下生效;使用者要長時間掛著建議用自動模式。
4. **開 USB 序列埠會重開裝置**:macOS 上 pyserial 即使先設 DTR/RTS=False,開 port 仍觸發 `rst:0x15`。已登 TRAPS `pyserial-open-resets-usj`(probable)。
5. **社群市集投稿第一次上傳逾時**:`submit --confirmed` 回 `The write operation timed out`;依官方 skill 先跑 `projects` 確認伺服器端沒有專案，
   才用相同內容重送一次，成功。重送前一定要先查，避免重複建立專案。

## 交付 / Commits

2d38a45..4470b63(展示頁、電量與熄屏省電功能、TRAPS 條目)

## 驗證證據

- 刷機:app 映像 SHA-256 前綴 `5430e762…`(validate.sh 歸檔，`archive_firmware.py verify` PASS),只寫 0x10000,`Hash of data verified`;
  重開後自動重連並維持加密綁定(`enc=1 auth=1 bonded=1 key=16`),配對與名牌內容保留。
- 熄屏/喚醒(真機 log):請求低功耗後 0.3 s 內 Mac 給 `interval 120.00 ms, latency 4, timeout 4000 ms`;喚醒後 1.5 s 內回到 `interval 30.00 ms, latency 0`。
- 使用者確認：熄屏 → 喚醒 → 按住 OK 說話，聽寫正常;App「裝置」分頁顯示電壓。
- 市集投稿:伺服器回傳 status `pending`,韌體 SHA-256 與 Release v0.1.0 那份(build 81da6a8)一致;投稿內容與預覽相同。
- 未驗證：熄屏實際電流與續航改善(沒有量測設備)、電量計誤讀何時再發生、Mac 拒絕連線參數時的退避路徑。

## 未完 / 交接

- GitHub Release v0.1.0 與市集投稿都還是舊韌體(81da6a8),不含本次修正;要更新需出 v0.1.1,市集用 `submit --project-id <既有專案>` 更新同一件，不要另開新件。
- 上游 PR(FoloToy/ai-passport:`docs/reference/<username>/badgemic/` 作品說明 + 2–3 則經驗條目)尚未開始;開 PR 前先給使用者看內容。
- 本次 commit 尚未 push 到 GitHub。
