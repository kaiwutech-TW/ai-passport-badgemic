import Foundation
import Testing
@testable import BadgeMicCore

@Suite struct CRCTests {
    @Test func knownVectors() {
        #expect(CRC32.checksum(Array("123456789".utf8)) == 0xCBF4_3926)
        #expect(CRC32.checksum([UInt8]()) == 0)
        #expect(CRC32.checksum(Array("a".utf8)) == 0xE8B7_BE43)
        #expect(CRC32.checksum(Array("The quick brown fox jumps over the lazy dog".utf8)) == 0x414F_A339)
        #expect(CRC32.checksum([UInt8](repeating: 0, count: 32)) == 0x190A_55AD)
    }
}

@Suite struct RGB565Tests {
    @Test func pixels() {
        // red, green, blue, white, black
        let rgba: [UInt8] = [255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255]
        let out = RGB565.convert(rgba: rgba, width: 5, height: 1)
        #expect(out == [0x00, 0xF8, 0xE0, 0x07, 0x1F, 0x00, 0xFF, 0xFF, 0x00, 0x00])
    }

    @Test func rowMajorAndSize() {
        var rgba = [UInt8](repeating: 0, count: 240 * 320 * 4)
        let p = (1 * 240 + 2) * 4 // row 1, column 2
        rgba[p] = 255
        let out = RGB565.convert(rgba: rgba, width: 240, height: 320)
        #expect(out.count == RGB565.pageByteCount && out.count == 153_600)
        let o = (1 * 240 + 2) * 2
        #expect(out[o] == 0x00 && out[o + 1] == 0xF8)
        #expect(out.filter { $0 != 0 }.count == 1)
    }
}

@Suite struct UploadTests {
    @Test func chunking() {
        let chunks = UploadPlanner.plan(totalSize: RGB565.pageByteCount)
        #expect(chunks.count == 686) // ceil(153600 / 224)
        #expect(chunks.allSatisfy { $0.length <= 224 && $0.length > 0 })
        #expect(chunks.reduce(0) { $0 + $1.length } == 153_600)
        var expected = 0
        for c in chunks { #expect(c.offset == expected); expected += c.length }
        #expect(chunks.last == UploadChunk(offset: 685 * 224, length: 153_600 - 685 * 224))
    }

    @Test func payloadAndFrameFit() throws {
        let data = [UInt8](repeating: 9, count: 224)
        let payload = UploadPlanner.putDataPayload(offset: 0x0102_0304, data: data[...])
        #expect(Array(payload[0..<4]) == [1, 2, 3, 4])
        #expect(payload.count == 228)
        #expect(try FrameCodec.encode(type: .putData, payload: payload).count == 234)
    }

    @Test func emptyAndExact() {
        #expect(UploadPlanner.plan(totalSize: 0).isEmpty)
        #expect(UploadPlanner.plan(totalSize: 224).count == 1)
        #expect(UploadPlanner.plan(totalSize: 225).count == 2)
    }
}

@Suite struct SeqTests {
    @Test func gapsAndWrap() {
        var t = SeqTracker()
        #expect(t.observe(254) == 0)
        #expect(t.observe(255) == 0)
        #expect(t.observe(0) == 0)
        #expect(t.observe(3) == 2)
        #expect(t.observe(3) == 0) // duplicate
        #expect(t.lost == 2 && t.received == 5)
    }
}

@Suite struct MessageTests {
    @Test func parseHelloWithDisplayObject() {
        let json = #"{"ev":"hello","proto":1,"fw":"0.1.0","model":"Passport-3A2B","caps":["opus","put","display"],"page":{"w":240,"h":320,"fmt":"rgb565le","count":2},"battery":87,"display":{"mode":"auto","dim_s":30,"off_s":120,"sleep_min":30,"brightness":80,"dim_brightness":20}}"#
        guard case .hello(let h)? = DeviceEvent.parse(Array(json.utf8)) else { Issue.record("no hello"); return }
        #expect(h.proto == 1 && h.fw == "0.1.0" && h.battery == 87)
        #expect(h.page?.count == 2)
        #expect(h.display == DisplaySettings.defaults)
    }

    @Test func parseHelloLegacyDisplayString() {
        let json = #"{"ev":"hello","proto":1,"fw":"x","model":"m","caps":[],"battery":-1,"display":"always_on"}"#
        guard case .hello(let h)? = DeviceEvent.parse(Array(json.utf8)) else { Issue.record("no hello"); return }
        #expect(h.display?.mode == .alwaysOn)
    }

    @Test func parseEvents() {
        func p(_ s: String) -> DeviceEvent? { DeviceEvent.parse(Array(s.utf8)) }
        #expect(p(#"{"ev":"ptt_start","id":4}"#) == .pttStart(id: 4))
        #expect(p(#"{"ev":"ptt_end","id":4,"frames":20,"dropped":1,"reason":"link"}"#) == .pttEnd(id: 4, frames: 20, dropped: 1, reason: "link"))
        #expect(p(#"{"ev":"key","key":"up","gesture":"long"}"#) == .key(.up, .long))
        #expect(p(#"{"ev":"put_ready","page":0}"#) == .putReady(page: 0))
        #expect(p(#"{"ev":"put_done","page":1,"ok":false,"err":"crc"}"#) == .putDone(page: 1, ok: false, err: "crc"))
        #expect(p(#"{"ev":"battery","battery":86}"#) == .battery(86))
        #expect(p(#"{"ev":"error","cmd":"foo","err":"unknown"}"#) == .error(cmd: "foo", err: "unknown"))
        #expect(p(#"{"ev":"display","mode":"always_on","brightness":50}"#) == .display(DisplaySettings(mode: .alwaysOn, brightness: 50)))
        #expect(p(#"{"ev":"zzz"}"#) == .unknown("zzz"))
        #expect(p("not json") == nil)
    }

    @Test func controlEncoding() throws {
        func s(_ c: ControlCommand) throws -> String { String(decoding: try c.jsonBytes(), as: UTF8.self) }
        #expect(try s(.hello(version: "0.1.0")) == #"{"app":"BadgeMic","cmd":"hello","proto":1,"ver":"0.1.0"}"#)
        #expect(try s(.pttAck(id: 3)) == #"{"cmd":"ptt_ack","id":3}"#)
        #expect(try s(.putBegin(page: 0, size: 153_600, crc32: 3_735_928_559)) == #"{"cmd":"put_begin","crc32":3735928559,"page":0,"size":153600}"#)
        #expect(try s(.putEnd(page: 0)) == #"{"cmd":"put_end","page":0}"#)
        #expect(try s(.clearPage(1)) == #"{"cmd":"clear_page","page":1}"#)
        #expect(try s(.setDisplay(DisplaySettings(mode: .alwaysOn))) == #"{"cmd":"set_display","mode":"always_on"}"#)
        let f = try ControlCommand.pttAck(id: 1).frame()
        #expect(f[0] == 0xA5 && f[2] == 0x03)
    }
}

@Suite struct DisplayTests {
    @Test func validation() {
        #expect(DisplaySettings.defaults.validationError() == nil)
        #expect(DisplaySettings(dimS: 4).validationError() != nil)
        #expect(DisplaySettings(dimS: 601).validationError() != nil)
        #expect(DisplaySettings(dimS: 60, offS: 30).validationError() != nil)
        #expect(DisplaySettings(offS: 3601).validationError() != nil)
        #expect(DisplaySettings(sleepMin: 0).validationError() == nil)
        #expect(DisplaySettings(sleepMin: 3).validationError() != nil)
        #expect(DisplaySettings(sleepMin: 241).validationError() != nil)
        #expect(DisplaySettings(brightness: 9).validationError() != nil)
        #expect(DisplaySettings(brightness: 50, dimBrightness: 60).validationError() != nil)
        #expect(DisplaySettings(brightness: 50, dimBrightness: 5).validationError() == nil)
    }

    @Test func changesAndMerge() {
        let base = DisplaySettings.defaults
        var edited = base
        edited.brightness = 60
        edited.mode = .alwaysOn
        let diff = edited.changes(from: base)
        #expect(diff == DisplaySettings(mode: .alwaysOn, brightness: 60))
        #expect(base.merged(with: diff) == edited)
        #expect(edited.changes(from: edited).isEmpty)
    }

    @Test func clamping() {
        let c = DisplaySettings(dimS: 1, offS: 2, sleepMin: 3, brightness: 500, dimBrightness: 99).clamped()
        #expect(c.dimS == 5 && c.offS == 5 && c.sleepMin == 5 && c.brightness == 100 && c.dimBrightness == 99)
        #expect(c.validationError() == nil)
    }

    @Test func codecRoundTrip() throws {
        let d = DisplaySettings(mode: .auto, dimS: 45, offS: 200, sleepMin: 0, brightness: 70, dimBrightness: 10)
        let data = try JSONEncoder().encode(d)
        let text = String(decoding: data, as: UTF8.self)
        #expect(text.contains("\"dim_s\":45") && text.contains("\"sleep_min\":0"))
        #expect(try JSONDecoder().decode(DisplaySettings.self, from: data) == d)
    }
}

@Suite struct ModelTests {
    @Test func profilePersistence() throws {
        var s = ProfileSettings.defaults
        #expect(s.profiles.map(\.name) == ["學舌鳥 Parroting", "豆包 Doubao", "Custom"])
        #expect(s.profiles[0].hotkey.isFn && s.profiles[0].behavior == .hold)
        #expect(s.profiles[0].leadMs == 200 && s.profiles[0].tailMs == 300)
        s.profiles[2].hotkey = Hotkey(keyCode: 49, modifiers: [.command, .shift])
        s.profiles[2].switchSystemInput = true
        s.profiles[2].outputDeviceUID = "BlackHole2ch_UID"
        s.selectedID = s.profiles[2].id
        let back = SettingsCodec.decodeProfiles(try SettingsCodec.encode(s))
        #expect(back == s)
        #expect(back.selected.name == "Custom")
    }

    @Test func profileFallbacks() {
        #expect(SettingsCodec.decodeProfiles(nil) == .defaults)
        #expect(SettingsCodec.decodeProfiles(Data("garbage".utf8)) == .defaults)
        var s = ProfileSettings.defaults
        s.selectedID = UUID()
        #expect(SettingsCodec.decodeProfiles(try? SettingsCodec.encode(s)).selectedID == s.profiles[0].id)
    }

    @Test func buttonMappingsRoundTrip() throws {
        var m = ButtonMappings()
        m[ButtonSlot(.up, .click)] = .keyShortcut(Hotkey(keyCode: 126, modifiers: [.option]))
        m[ButtonSlot(.up, .long)] = .typeText("你好, 世界")
        m[ButtonSlot(.down, .click)] = .runShortcut("My Shortcut")
        m[ButtonSlot(.down, .long)] = .openApp("Safari")
        let back = SettingsCodec.decodeMappings(try SettingsCodec.encode(m))
        #expect(back == m)
        #expect(back.action(for: .down, .long) == .openApp("Safari"))
        #expect(ButtonMappings().action(for: .up, .click) == .none)
    }

    @Test func vCard() {
        var b = BadgeInfo()
        b.name = "王小明"; b.title = "工程師"; b.organization = "Acme, Inc."; b.extraLine = "hi"
        let v = b.qrPayload
        #expect(v.contains("FN:王小明") && v.contains("ORG:Acme\\, Inc.") && v.contains("TITLE:工程師"))
        b.qrMode = .custom; b.customQR = "https://example.com"
        #expect(b.qrPayload == "https://example.com")
        #expect(SettingsCodec.decodeBadge(try? SettingsCodec.encode(b)) == b)
    }
}
