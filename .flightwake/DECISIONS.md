<!-- flightwake DECISIONS — append-only。一行一決策,新的加在最上面。 -->
<!-- 什麼算「決策」:關掉了其他選項的選擇。格式:日期 | 決策 | why | 觸發重評條件(選填) -->
<!-- 推翻決策:不刪舊行 — 新決策一行寫明取代哪天的哪條;舊行「決策」欄開頭加 [superseded→新日期]。新舊衝突時以此判方向。 -->

# 決策日誌

| 日期 | 決策 | 為什麼 | 重評條件 |
|---|---|---|---|
| 2026-11-13 | 名牌每一頁由 Mac App 渲染成 240×320 RGB565 圖上傳,裝置只顯示圖片 + 疊加電量/藍牙/錄音指示;不在裝置上排版使用者文字 | 姓名可能是任意繁中字,裝置端中文字型覆蓋不了;QR、照片、版面也都交給 Mac 做,韌體最小 | 需要裝置脫離 Mac 自行編輯名牌時 |
| 2026-11-13 | 分區表沿用老人安心牌的相容配置:factory 3 MB(0x10000);`cardid`(0x356000)與 `recovery`(0x700000)照宣告但韌體不寫;名牌圖存在新 `badge` 資料分區(0x360000 起、1 MB、雙槽 + CRC) | 合併映像只蓋到 app 結尾,不會碰出廠身份與 Recovery,保留官方恢復路徑 | app 超過 3 MB,或確認官方恢復不依賴這兩區時 |
| 2026-11-13 | OK:按下立即預錄,<300 ms 放開 = 輕點換名牌頁,按滿 300 ms 才送 ptt_start;UP/DOWN 一律回報給 Mac(click/long≥600 ms) | 輕點與說話共用一顆鍵又不誤觸發聽寫,且不漏掉開頭的字 | 實測 300 ms 太短或太長時 |
| 2026-11-13 | 省電:auto = 30 s 調暗、120 s 熄屏但 BLE 保持(OK 隨按隨說)、閒置 30 分深睡;always_on 由 Mac App 切換、電量 <15% 自動退出 | 使用者要 2 分鐘熄屏 + 常亮模式;熄屏若直接深睡,麥克風每次都要重開機重連 | 實測待機耗電過高時 |
| 2026-11-13 | 辨識工具不綁定:Mac App 以「觸發設定檔」(快捷鍵、按住或切換、麥克風來源)描述每個工具,學舌鳥、豆包、Mac 聽寫都只是預設檔;取代同日「首個目標學舌鳥」那條 | 使用者明說不糾結學舌鳥,會在另一台 MacBook Air 測豆包 | 某工具無法用模擬按鍵觸發時,為它加專屬觸發方式 |
| 2026-11-13 | repo 結構:本機 `main` 對齊 `upstream/main`(FoloToy/ai-passport),開發在 `feature/badge-mic`;舊 main(含 Codex 的 261111 record)留在 `archive/pre-upstream-main` | 上游規定 main 保持乾淨基線、應用走 feature/*;舊 main 只有空 commit + 一份 record,已 cherry-pick 到 feature 分支 | 要推到使用者自己的 GitHub 時 |
| 2026-11-13 | Mac 端 companion App 以 MIT 授權、參考 SayAll(GPL-3.0)的做法自行重寫,不複製其程式碼 | 使用者要 MIT;複製 GPL 程式碼會讓整個 App 變 GPL | 想直接沿用 SayAll 大段程式碼時 |
| 2026-11-13 | [superseded→2026-11-13 辨識工具不綁定] 語音轉文字交給使用者自選的辨識工具:Mac App 把 BLE 收到的聲音寫進虛擬麥克風(先用 BlackHole 2ch)並模擬該工具的快捷鍵;首個目標學舌鳥(Parroting)= 按住 Fn 收音、放開結束 | 使用者要像 SayAll 一樣可換辨識工具;裝置端不做 STT、不連 Wi-Fi | 合成的 Fn 事件無法觸發學舌鳥時(需改用虛擬 HID 等路線) |
| 2026-11-13 | 按鍵:OK = 按住說話(push-to-talk);UP / DOWN 由使用者在 Mac App 設定功能 | 使用者指定 | — |
| 2026-11-13 | 產品範圍只做兩件事:名牌(直式)+ 按住說話把文字送進電腦的麥克風;不做 Gemini/ChatGPT 語音對話 | 使用者明說「這兩個功能就夠了」;訂閱帳號無官方接入路徑,API 計費另付 | 使用者想加 AI 對話,或官方開放訂閱帳號給第三方裝置時 |
| 2026-11-13 | 虛擬麥克風改用自寫的 AudioServerPlugIn 回環驅動「BadgeMic Microphone」(`companion/macos/Driver/`,MIT、只看 Apple 公開標頭),不再依賴 BlackHole / MiRemoteV;App 自動模式先找它,找不到才找其他 loopback,都沒有就不播(不送喇叭) | 目標 MacBook Air 只會裝我們的 App;BlackHole 是 GPL-3.0 不能打包進 MIT App | 驅動在真 coreaudiod 載不起來,或 macOS 改用 DriverKit/Audio Server 新機制時 |
| 2026-11-13 | 長按關螢幕由裝置本地處理(`set_display.off_key`,存 NVS),不走 Mac 按鍵映射 | 要在沒連 Mac 時也能關;Mac 端只負責設定 | — |
| 2026-11-13 | App 與驅動用真憑證簽名(Developer ID 優先,否則 Apple Development;以 hash 選、不寫名字進 repo),不再 ad-hoc | ad-hoc 的 DR 是 cdhash,每次重編掉輔助使用授權;使用者有 Apple 開發者帳號 | 要在別台 Mac 免右鍵開啟時 → 需 Developer ID Application 憑證 + notarize |
| {{YYYY-MM-DD}} | {{採用 X 而非 Y}} | {{一句話}} | {{什麼情況要回頭看(選填)}} |
