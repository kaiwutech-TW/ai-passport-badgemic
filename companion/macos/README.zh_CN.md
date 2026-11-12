<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# BadgeMic(macOS 伴侣应用)

配合 FoloToy AI Passport 的 Badge Mic 固件使用的菜单栏应用。工牌是一支 BLE 按住说话麦克风,同时是电子名牌;本应用:

1. 接收工牌音频(BLE 上的 Opus),在按住听写工具热键的同时,把声音播放到**虚拟麦克风**(例如 BlackHole 2ch),
   让学舌鸟(Parroting)、豆包等工具把文字打出来;
2. 把 UP/DOWN 按键映射为快捷键、文字片段、「快捷指令」或打开应用;
3. 渲染并上传名牌页面(照片/姓名/职称卡与 QR 卡),格式为 240x320 RGB565;
4. 控制屏幕模式(自动 / 常亮、调暗与休眠计时、亮度)。

通信协议见 [`docs/development/engineering/badge-mic-protocol.md`](../../docs/development/engineering/badge-mic-protocol.md)。

## 环境要求

- macOS 14 及以上、Command Line Tools(**不需要** Xcode)、Swift 6 工具链。
- Homebrew `opus`(`brew install opus`)。
- 虚拟麦克风驱动,例如 `brew install blackhole-2ch`。

## 构建

```sh
cd companion/macos
./scripts/build-app.sh        # -> dist/BadgeMic.app(release,内置 libopus,ad-hoc 签名)
./scripts/test.sh             # 运行 Swift Testing 测试(遇到已知的 CLT 偶发问题会重试)
swift build                   # 普通 debug 构建
```

测试使用 Swift Testing(仅有 Command Line Tools 时没有 XCTest)。`test.sh`
会自动重试,因为在 Command Line Tools 下 `swift test` 偶尔会找不到 Swift Testing 的宏插件(非确定性,重试即可)。

目录:`BadgeMicCore`(分帧、协议、CRC-32、RGB565、上传分块、按住说话状态机、数据模型;不依赖 AppKit/CoreBluetooth)、
`BadgeMicOpus`(libopus 封装)、`BadgeMicRender`(页面渲染)、`BadgeMic`(BLE、音频输出、按键注入、SwiftUI)。

## 权限与设置

1. **蓝牙**:macOS 询问时允许。首次连接时 macOS 会弹出配对窗口,请输入工牌屏幕上的 6 位数字密码(特征值要求加密)。
2. **辅助功能**(系统设置 > 隐私与安全性 > 辅助功能):注入热键所必需。「聽寫」分页会显示状态。授权后请重启应用。
3. **虚拟麦克风**:安装 BlackHole 2ch。在 BadgeMic 的「聽寫」分页把它选为输出设备;在听写工具里把麦克风选为
   **BlackHole 2ch**(或在设置档中开启「切换系统输入」,BadgeMic 只在一轮语音期间把它设为默认输入,结束后还原)。
4. 选择触发设置档(学舌鸟:Fn 按住;豆包:Fn 按住;Custom:任意按键 + 修饰键,按住或切换)。
   可用「测试音」「测试热键」在没有工牌时检查路由。

安全:热键会在 `ptt_end`、BLE 断线、一轮中 2 秒没有音频/事件、以及应用退出(含 SIGTERM/SIGINT)时释放。

## 使用

- 按住工牌 **OK**:音频串流到 Mac,听写工具打字。轻点 OK 切换工牌页面。
- 「按鍵」分页:设置 UP/DOWN 的短按、长按动作。
- 「名牌」分页:编辑字段、选照片、选择 vCard 或自定义 QR 内容、实时预览、上传。
- 「裝置」分页:连接状态、电量、固件、最近一轮统计、屏幕设置(只送出有变更的字段)。

## 许可

MIT,见 [LICENSE](LICENSE)。本代码为独立实现,未使用任何 GPL 代码。
