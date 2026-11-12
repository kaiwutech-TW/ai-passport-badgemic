<p align="right">
  <strong>简体中文</strong> · <a href="badge-mic-protocol.md">English</a>
</p>

# Badge Mic 线协议（设备 ↔ Mac）

本文是 Badge Mic 固件（`feature/badge-mic` 分支的 `main/`）与 macOS 配套应用
（`companion/macos/`）之间的契约，两边须在同一次迭代中一起修改。分帧格式改编自
Shinku-Chen 以 MIT 许可发布的 Pocket Intercom 协议（`Shinku-Chen/ai-passport`
分支 `feature/openclaw-intercom` 的 `docs/development/engineering/intercom-wire-protocol.md`）。

## 协议服务的产品行为

- 设备是竖屏名牌。每一页名牌由 Mac 渲染成 240 × 320 图片后上传；设备从不排版用户文字，
  因此任何字体、文字或二维码都无需设备端字形覆盖。
- 按住 **OK** 时把麦克风音频传给 Mac。Mac 写入虚拟麦克风（如 BlackHole 2ch），并按住用户
  所选听写工具的快捷键（默认按住 `Fn`），由该工具打出文字。
- **UP**、**DOWN** 上报手势，由 Mac 映射到用户自定义的动作。

## 角色与传输

| 项目 | 值 |
| --- | --- |
| 设备角色 | BLE 外设，可连接广播，一个连接 |
| Mac 角色 | BLE 中心（CoreBluetooth） |
| 广播名 | `Passport-XXXX`（BT MAC 末两字节，大写十六进制） |
| 服务 | Nordic UART Service `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| Mac → 设备 | `6e400002-b5a3-f393-e0a9-e50e24dcca9e`（RX）：`WRITE`、`WRITE_NO_RSP`，需加密 |
| 设备 → Mac | `6e400003-b5a3-f393-e0a9-e50e24dcca9e`（TX）：`NOTIFY`，需加密 |
| 首选 ATT MTU | 247 |
| 安全 | LE Secure Connections、MITM、bonding；设备显示 6 位配对码（≥ 100000） |

只有在已连接、已加密、已订阅且双方 hello 都交换完成后，链路才算就绪。

## 帧格式

```text
[0] 0xA5  [1] 0x5A  [2] TYPE  [3] FLAGS  [4..5] LEN（大端）  [6..] payload
```

接收端逐字节扫描，直到 magic、已知类型与合法长度同时成立；若半截帧中出现新的合法帧头，
丢弃半截帧。

| 类型 | 名称 | 方向 | 载荷 | 载荷上限 |
| --- | --- | --- | --- | --- |
| `0x03` | `CONTROL` | Mac → 设备 | UTF-8 JSON 对象 | 512 |
| `0x04` | `EVENT` | 设备 → Mac | UTF-8 JSON 对象 | 512 |
| `0x05` | `AUDIO_OPUS` | 设备 → Mac | `[SEQ:1]` + 一个 Opus 包 | 1 + 512 |
| `0x07` | `PUT_DATA` | Mac → 设备 | `[OFFSET:4 大端]` + 正在上传资源的字节 | 4 + 224 |

标志位（`0x01 MORE`、`0x02 FIRST`、`0x04 LAST`）保留；本文定义的帧都是单帧消息，标志为 `0`。

## 握手

设备在订阅完成后以 `EVENT` 发送：

```json
{"ev":"hello","proto":1,"fw":"0.1.0","model":"Passport-3A2B",
 "caps":["opus","put","display"],
 "page":{"w":240,"h":320,"fmt":"rgb565le","count":2},
 "battery":87,"display":"auto"}
```

Mac 看到设备 hello 后以 `CONTROL` 发送：

```json
{"cmd":"hello","proto":1,"app":"BadgeMic","ver":"0.1.0"}
```

`battery` 为百分比，不可用时为 `-1`。看到不同 `proto` 的一方必须显示可见的版本不符提示，
且不得传输音频。

## 按住说话

音频：Opus，16 kHz，单声道，60 ms 帧（960 采样），`OPUS_APPLICATION_VOIP`，complexity 0，
开启 DTX。`SEQ` 每个音频帧加一，256 回绕，只用于丢帧统计。音频从不重传。

流程：

1. OK 按下，设备立即开始采集并写入预录缓冲。
2. 若在 **300 ms** 内松开，视为轻点：不发送任何事件，丢弃缓冲音频，设备切换到下一页名牌。
3. 按住满 300 ms 时，设备发送 `{"ev":"ptt_start","id":N}`，接着发送缓冲帧，再发送实时帧。
4. 松开时设备发出最后一帧，再发送
   `{"ev":"ptt_end","id":N,"frames":F,"dropped":D}`。
5. 若链路断开或按住超过 **120 s**，设备以
   `{"ev":"ptt_end","id":N,"frames":F,"dropped":D,"reason":"timeout"|"link"}` 结束本轮。

`id` 每轮递增。Mac 必须在 `ptt_end`、断开连接、以及本轮中 2 s 内没有任何音频或事件到达时
松开听写快捷键，确保按键永远不会卡在按下状态。

Mac 可在快捷键已按住且虚拟麦克风开始接收后回复 `{"cmd":"ptt_ack","id":N}`；设备只用它把
录音指示从「采集中」切换为「直播中」。

## 按键

```json
{"ev":"key","key":"up","gesture":"click"}
```

`key` 为 `up` 或 `down`；`gesture` 为 `click` 或 `long`（按住 ≥ 600 ms，松开时上报）。OK 永远
不产生 `key` 事件。屏幕熄灭时，第一次按 UP 或 DOWN 只点亮屏幕、不上报。屏幕熄灭时按住 OK
照常工作并点亮屏幕。

## 显示模式

```json
{"cmd":"set_display","mode":"auto"}
```

`auto`：闲置 30 s 调暗、120 s 熄屏（BLE 链路保持，OK 立即可用）、闲置 30 分钟深睡。
`always_on`：屏幕永不熄灭；电量低于 15 % 时设备自行退出 `always_on`。设备持久保存模式，
变化时上报 `{"ev":"display","mode":"always_on"}`。

## 名牌页面上传

每页正好 `w × h × 2` 字节（240 × 320 为 153 600），RGB565、小端、逐行、无文件头。开机显示
第 `0` 页；轻点 OK 在已有有效图片的页面之间循环。

1. Mac：`{"cmd":"put_begin","page":0,"size":153600,"crc32":3735928559}`
   （CRC-32/ISO-HDLC，即 zlib 的 `crc32`）。
2. 设备准备一个非活动槽位并回复 `{"ev":"put_ready","page":0}`，或
   `{"ev":"put_done","page":0,"ok":false,"err":"..."}`。
3. Mac 按偏移顺序以带响应写入发送 `PUT_DATA` 帧，每帧最多 224 个数据字节。
4. Mac：`{"cmd":"put_end","page":0}`。
5. 设备校验大小与 CRC，仅在成功时启用该槽位；若该页正在显示则重绘，并回复
   `{"ev":"put_done","page":0,"ok":true}`。

`{"cmd":"clear_page","page":1}` 删除一页。失败或中断的上传永远不会替换原先启用的图片。

## 状态

电量百分比变化时，设备发送 `{"ev":"battery","battery":86}`，每分钟最多一次。

## 错误处理

未知命令回复 `{"ev":"error","cmd":"<名称>","err":"unknown"}`。JSON 格式错误时记录日志并忽略。
未知类型的帧视为错位，由重组器跳过。
