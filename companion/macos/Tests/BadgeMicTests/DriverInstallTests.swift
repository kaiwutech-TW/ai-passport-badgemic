import Testing
@testable import BadgeMicCore

@Suite struct DriverInstallTests {
    @Test func commandQuotesPathsAndRestartsAudio() {
        let cmd = DriverInstall.shellCommand(source: "/Applications/Badge Mic.app/Contents/Resources/BadgeMicAudio.driver")
        #expect(cmd.contains("cp -R '/Applications/Badge Mic.app/Contents/Resources/BadgeMicAudio.driver' '/Library/Audio/Plug-Ins/HAL/BadgeMicAudio.driver'"))
        #expect(cmd.hasPrefix("mkdir -p '/Library/Audio/Plug-Ins/HAL' && rm -rf '/Library/Audio/Plug-Ins/HAL/BadgeMicAudio.driver'"))
        #expect(cmd.hasSuffix("(killall coreaudiod || launchctl kickstart -kp system/com.apple.audio.coreaudiod)"))
    }

    @Test func singleQuoteInPathIsEscaped() {
        let cmd = DriverInstall.shellCommand(source: "/tmp/it's/x.driver")
        #expect(cmd.contains("'/tmp/it'\\''s/x.driver'"))
    }

    @Test func appleScriptEscapesQuotes() {
        let s = DriverInstall.appleScript(source: "/a\"b", prompt: "安裝")
        #expect(s.hasPrefix("do shell script \""))
        #expect(s.contains("/a\\\"b"))
        #expect(s.hasSuffix("with prompt \"安裝\" with administrator privileges"))
    }
}
