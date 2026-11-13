<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# BadgeMic 虚拟音频驱动

`BadgeMicAudio.driver` 是一个最小的 macOS Core Audio 回环设备(AudioServerPlugIn,由 `coreaudiod`
加载)。它提供一个设备 **BadgeMic Microphone**:一路 2 声道 32 位浮点输出流、一路 2 声道 32 位浮点
输入流,48000 Hz(也支持 44100 Hz)。写入输出端的音频经约 1.4 秒、按设备采样时间索引的环形缓冲
从输入端读回;没写入的时段读到静音。

BadgeMic 应用把名牌音频播放到输出端,听写应用把 “BadgeMic Microphone” 选为麦克风。该设备不会成为
默认输出或系统输出设备,但可以被选为输入设备。

设备 UID `BadgeMicLoopback_UID`,型号 UID `BadgeMicLoopback_ModelUID`,厂商 `BadgeMic`,
bundle id `tw.kaiwu.badgemic.audio`。

## 构建

只需要 Xcode Command Line Tools(不需要 Xcode)。

    ./build.sh

输出:`../dist/BadgeMicAudio.driver`(arm64 + x86_64 通用,macOS 13+)。签名身份依次为:
`$BADGEMIC_SIGN_IDENTITY`(哈希或名称)、第一个 “Developer ID Application”、第一个
“Apple Development”,都没有时使用 ad-hoc。

## 不安装即可测试

    ./tests/run.sh

构建驱动后用 `dlopen` 加载二进制,经伪造的宿主接口驱动它,检查属性、流格式、零时间戳、采样率切换,
以及正弦波的写入/读回。

## 安装 / 卸载

安装系统音频驱动需要管理员权限(sudo)。

    ./install.sh      # 复制到 /Library/Audio/Plug-Ins/HAL 并重启 coreaudiod
    ./uninstall.sh    # 删除并重启 coreaudiod

重启 `coreaudiod` 会让整台 Mac 的声音短暂中断。若不允许 `killall coreaudiod`,脚本改用
`launchctl kickstart -kp system/com.apple.audio.coreaudiod`。

## 净室说明

本驱动只参考 Apple 公开的 SDK 头文件(`CoreAudio/AudioServerPlugIn.h`、`AudioHardwareBase.h` 等)
与 Apple 文档从零编写,未阅读或复制任何第三方驱动源码(包括 GPL 项目)。以 MIT 许可证发布。

## 源码结构

- `BadgeMicAudio.c`:整个插件(纯 C)
- `Info.plist`:bundle 元数据与 CFPlugIn 工厂注册
- `build.sh`、`install.sh`、`uninstall.sh`
- `tests/test_driver.c`、`tests/run.sh`:主机端测试
