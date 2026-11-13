<p align="right"><a href="README.md">English</a> · <a href="README.zh_TW.md">繁體中文</a> · <strong>简体中文</strong></p>

# AI Passport Badge Mic

**网站：[badgemic.kaiwu.com.tw](https://badgemic.kaiwu.com.tw)**

把 [FoloToy AI Passport](https://github.com/FoloToy/ai-passport) 变成两样可以戴在身上的东西：

- **工牌**：竖版名片页加一个二维码（你的联系人 vCard，或任意链接），在 Mac 上设计好再传上去。
- **Mac 听写用的无线按键麦克风**：按住 OK 说话，声音经蓝牙送到配套应用 **BadgeMic**。应用会把声音播进虚拟麦克风，
  同时按住你的听写工具快捷键。松开按键，文字就打在你正在输入的地方。

只要听写工具有快捷键、能选麦克风就能用，例如学舌鸟（Parroting）、豆包、macOS 自带听写。
除非你的听写工具自己上传，声音不会发送到任何云端服务。

> 这是基于 FoloToy 开源固件模板的社区项目，不是 FoloToy 官方产品，也不由 FoloToy 提供支持。

## 需要准备

- FoloToy AI Passport（ESP32-C3）和一根能传数据的 USB-C 线
- Apple 芯片的 Mac，macOS 14 及以上
- 一个有「按住说话」或「按一下开始/结束」快捷键的听写工具

## 快速开始

### 1. 先备份，再刷工牌固件

刷机会替换出厂固件。请先完整备份 Flash，以后才能还原：

```sh
pip install esptool
# 找串口：ls /dev/cu.usbmodem*
esptool --chip esp32c3 -p /dev/cu.usbmodemXXXX read_flash 0 0x800000 passport-backup.bin
```

备份文件包含设备身份，请自行保管、不要分享。然后从 [Releases](https://github.com/kaiwutech-TW/ai-passport-badgemic/releases) 下载固件并刷入：

```sh
esptool --chip esp32c3 -p /dev/cu.usbmodemXXXX -b 460800 write_flash 0x0 badgemic-passport-firmware-<版本>.bin
```

- **不要**执行 `erase_flash`，它会一并删掉设备身份和永久 Recovery 分区。
- 想回到官方固件：把备份写回 `0x0`，或按 FoloToy 的[恢复默认固件教程](https://ai-passport.folotoy.cn/guides/restore-factory-firmware/)操作。

### 2. 安装 Mac 应用

1. 从 [Releases](https://github.com/kaiwutech-TW/ai-passport-badgemic/releases) 下载 `BadgeMic-<版本>.dmg`，把 **BadgeMic** 拖进「应用程序」。
   应用已签名并通过 Apple 公证，可以直接打开。
2. 打开 BadgeMic（在菜单栏），进入 **「聽寫」**（听写）分页：
   - 点 **「安裝虛擬麥克風」**（安装虚拟麦克风）并输入密码，系统会多一个音频设备 **BadgeMic Microphone**。安装时 Mac 的声音会中断一两秒。
   - 点 **「請求授權」**（请求授权），到「系统设置 → 隐私与安全性 → 辅助功能」允许 BadgeMic。
     应用需要这个权限才能替你按听写快捷键。授权后请退出并重新打开 BadgeMic。
3. 配对工牌：BadgeMic 会自动找到工牌，macOS 要求配对码时，输入工牌屏幕上的 6 位数字。

（应用界面目前为繁体中文。）

### 3. 设置听写工具

1. 在 BadgeMic 的「聽寫」分页选触发配置（学舌鸟、豆包或自定义），设成你的工具使用的快捷键。
   按住才收音的工具选 **「按住」**，按一下开始、再按一下结束的工具选 **「切換」**（切换）。
2. 在 **听写工具自己的设置** 里，把麦克风选成 **BadgeMic Microphone**。很多工具有独立的麦克风设置，不跟随系统设置。
3. 按住工牌的 OK 说一句话再松开，文字应该就会出现。

### 4. 设计工牌

在 **「名牌」** 分页填写姓名、职位、公司、电话、Email、网站，可加照片，然后上传。
工牌有两页：名片页和二维码页，轻按 OK 切换。

## 使用方式

| 按键 | 动作 |
| --- | --- |
| OK 轻按 | 切到下一页 |
| OK 按住 | 说话：工牌显示麦克风与音量条，松开即发送 |
| UP / DOWN 短按或长按 | 在 **「按鍵」**（按键）分页自定义：快捷键、Return、Escape、文字片段、「快捷指令」、打开应用，或（长按）关闭工牌屏幕 |

- **屏幕**：在 **「裝置」**（设备）分页设置多久调暗、多久熄屏、亮度，或设为常亮。按任意键唤醒，第一下只负责唤醒。
- **太大声？** 声音削波时音量条会变红，到「裝置」分页调低 **「收音增益」**。
- **NFC**：背面的 NFC 是独立芯片，可用手机应用（例如 NFC Tools）写入任意链接，例如 LINE 或 LinkedIn 主页。
  写入前先读取并保存原内容，千万不要锁定标签。

## 从源码构建

- 固件：ESP-IDF 5.5。先读 [AGENTS.zh_CN.md](AGENTS.zh_CN.md)，再运行 `./tools/validate.sh`（主机测试 + 固件构建）。
- Mac 应用与虚拟麦克风驱动：见 [companion/macos/README.zh_CN.md](companion/macos/README.zh_CN.md)，只需要 Xcode Command Line Tools。
- 工牌 ↔ Mac 通信协议：[docs/development/engineering/badge-mic-protocol.zh_CN.md](docs/development/engineering/badge-mic-protocol.zh_CN.md)。
- FoloToy 原项目说明：[docs/README.zh_CN.md](docs/README.zh_CN.md)。

## 致谢与许可

MIT，见 [LICENSE](LICENSE)。基于 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport)（MIT）。
蓝牙串口通道改写自 Shinku-Chen 的 [Pocket Intercom](https://github.com/Shinku-Chen/ai-passport)（MIT）。「通过虚拟麦克风驱动听写工具」的思路来自
[SayAll](https://github.com/HD838A/remote-mic-app)；BadgeMic 及其虚拟麦克风驱动均为从零编写，不含其代码。
