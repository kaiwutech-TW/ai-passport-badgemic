---
updated: 2026-11-11
updated_by: Codex (工具用途調查)
latest_record: records/261111-tool-options.md
health: yellow  # 第一版 STATE,repo 尚無程式碼,沒有任何東西可驗證
---
<!-- flightwake STATE — 永遠短、永遠新。新 session 的第一站。 -->
<!-- 規則:只寫「現在」與「下一步」;歷史去 records/,決策去 DECISIONS.md。 -->
<!-- 冷啟動契約:讀完本檔 + latest_record 必須能在 5 分鐘內安全接手。 -->

# 現在在哪

repo 尚無應用程式碼。STATE 與首份調查 record 已納入版本控制，其餘 flightwake 與 agent 設定仍未追蹤。
目標(2026-11-11 使用者說明):替自己買的 FoloToy AI Passport(上游 https://github.com/FoloToy/ai-passport,
ESP32-C3、無 PSRAM、只有 BLE 沒有 Classic BT)做韌體:平常當工牌/名片,按鍵時接 Gemini 或 ChatGPT 語音對話。
可行性與實用工具候選調查已完成，來源與限制見 latest_record；尚未選定用途或連線路線。

# 進行中(未完成勿刪)

- [ ] 路線未定:等使用者回答手機平台(Android/iPhone)、API 計費能否接受、要不要脫離手機獨立使用
- [ ] 使用者正在探索其他實用工具用途，尚未承諾實作方向

# 下一步入口

1. 選定一個核心用途，再確認連線方式與技術棧；選型後才寫 README 與 DECISIONS
2. 尚未有測試、typecheck 或真機證據；不能把文件調查當驗收通過

# 常備事實(這個 repo 的 3-5 條保命知識)

- 不明(還沒有程式碼)
