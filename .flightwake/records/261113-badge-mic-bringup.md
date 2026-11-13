---
record_id: 261113-badge-mic-bringup
session: Claude Code (claude-opus-5-5) 2026-11-13
date: 2026-11-13
repos: [ai-passport]
tests: validate.sh PASS(firmware 649246e);companion 55 tests PASS;driver harness 760 checks PASS;真機端到端 PASS(使用者確認)
prod_changes: 真機刷寫 app 區 0x10000(649246e);本機安裝系統音訊驅動 /Library/Audio/Plug-Ins/HAL/BadgeMicAudio.driver
---

# Badge Mic 從協定到真機打通:韌體 + Mac App + 自寫虛擬麥克風

**TL;DR**:起點是只有範圍決策、沒有程式碼。終點:Passport 韌體(名牌 + 按住 OK 說話)、Mac App BadgeMic、自寫
AudioServerPlugIn 虛擬麥克風都已完成,並在本機真機打通——按住 OK 說話 → BLE Opus → BadgeMic → 「BadgeMic Microphone」→
學舌鳥打出字(不再依賴 SayAll 附帶的 MiRemoteV)。

## 關鍵發現(重要性排序)

1. **學舌鳥(Parroting)不跟系統輸入走,有自己的麥克風設定**(`settings_store.json` 的 `selected_microphone`)。只改系統設定的輸入裝置
   沒用;換成 BadgeMic Microphone 後才收得到。已登 TRAPS `parroting-own-mic-setting`。
2. 自寫驅動在真 coreaudiod 下可用,且 Apple Development 簽名即被接受(至少本機)。回環在「先播後錄」「先錄後播」、以及用 App 的
   `AudioOutput` 原碼重播,三種順序都 −6 dB 原樣回來;真機一輪約 12 秒語音全數進入驅動。
3. 熄屏 / 一段時間後 USB 序列埠消失又重現(開機後約 210 秒,最後一行 log 之後無 PM 降頻訊息)。IDF 的 USB-Serial-JTAG 連線監視器在連線
   時會保留 BBPLL,所以「降頻殺 USB」證據不足;根因未明,只影響看 log,不影響 BLE 與聽寫。
4. 真機語音峰值碰到 0 dBFS(會削波),要調低增益。
5. 韌體 I2S DMA 環只有約 90 ms(6×240 幀),先前量到單幀編碼最長 93 ms → 編碼任務優先序提到 6、堆疊 32 KB(先前只剩 472 B)。
   新版每輪統計(enc_first/avg/max、stack_free)因 USB 斷線**尚未讀到**。
6. 先前兩個 bug(已修於 fda4d3b):PTT 120 秒上限 uint32 下溢導致一按就送出;Mac 端 hello 競態卡「等待裝置 hello」→ 加 `get_hello` 重試。

## 交付 / Commits

b7dcd96..6940250(協定文件、Mac App、韌體、兩個真機 bug 修正、off_key 長按關螢幕、vCard 電話/Email/網站、虛擬麥克風自動偵測、
真憑證簽名、自寫驅動、App 圖示)。決策見 DECISIONS 2026-11-13 各列。

## 驗證證據

- 韌體:`./tools/validate.sh` PASS;封存 `build/firmware/ca8643ca…`,app sha256 `fad58f7e…`,版本字串 `649246e`(乾淨樹)。
  刷 0x10000 後開機 log:配對保留(`bonded=1`),藍牙後 heap 60 KB(舊版 68 KB,多出的 8 KB 為編碼堆疊)。
- 驅動:`system_profiler` 列出 BadgeMic Microphone(2 in/2 out、48 kHz、Virtual);ffmpeg 錄回測試音 −6 dB;真機 PTT 時錄到
  t=48–59 s 語音(mean −15~−30 dB)。
- 端到端:使用者確認學舌鳥麥克風改為 BadgeMic Microphone 後可正常打字。

## 未完 / 交接

- 讀到新韌體每輪編碼統計(需 USB log,或把統計放進 `ptt_end` 讓 Mac 記錄)
- USB 序列埠消失的根因
- 語音削波:降增益
- 未測:名牌 vCard 上傳、UP/DOWN 映射、off_key、深睡喚醒;簽名後重編是否仍保有輔助使用授權
- MacBook Air:需 Developer ID Application 憑證 + notarize 才能免右鍵;驅動安裝要管理員密碼
