<!-- flightwake DECISIONS — append-only。一行一決策,新的加在最上面。 -->
<!-- 什麼算「決策」:關掉了其他選項的選擇。格式:日期 | 決策 | why | 觸發重評條件(選填) -->
<!-- 推翻決策:不刪舊行 — 新決策一行寫明取代哪天的哪條;舊行「決策」欄開頭加 [superseded→新日期]。新舊衝突時以此判方向。 -->

# 決策日誌

| 日期 | 決策 | 為什麼 | 重評條件 |
|---|---|---|---|
| 2026-11-13 | repo 結構:本機 `main` 對齊 `upstream/main`(FoloToy/ai-passport),開發在 `feature/badge-mic`;舊 main(含 Codex 的 261111 record)留在 `archive/pre-upstream-main` | 上游規定 main 保持乾淨基線、應用走 feature/*;舊 main 只有空 commit + 一份 record,已 cherry-pick 到 feature 分支 | 要推到使用者自己的 GitHub 時 |
| 2026-11-13 | Mac 端 companion App 以 MIT 授權、參考 SayAll(GPL-3.0)的做法自行重寫,不複製其程式碼 | 使用者要 MIT;複製 GPL 程式碼會讓整個 App 變 GPL | 想直接沿用 SayAll 大段程式碼時 |
| 2026-11-13 | 語音轉文字交給使用者自選的辨識工具:Mac App 把 BLE 收到的聲音寫進虛擬麥克風(先用 BlackHole 2ch)並模擬該工具的快捷鍵;首個目標學舌鳥(Parroting)= 按住 Fn 收音、放開結束 | 使用者要像 SayAll 一樣可換辨識工具;裝置端不做 STT、不連 Wi-Fi | 合成的 Fn 事件無法觸發學舌鳥時(需改用虛擬 HID 等路線) |
| 2026-11-13 | 按鍵:OK = 按住說話(push-to-talk);UP / DOWN 由使用者在 Mac App 設定功能 | 使用者指定 | — |
| 2026-11-13 | 產品範圍只做兩件事:名牌(直式)+ 按住說話把文字送進電腦的麥克風;不做 Gemini/ChatGPT 語音對話 | 使用者明說「這兩個功能就夠了」;訂閱帳號無官方接入路徑,API 計費另付 | 使用者想加 AI 對話,或官方開放訂閱帳號給第三方裝置時 |
| {{YYYY-MM-DD}} | {{採用 X 而非 Y}} | {{一句話}} | {{什麼情況要回頭看(選填)}} |
