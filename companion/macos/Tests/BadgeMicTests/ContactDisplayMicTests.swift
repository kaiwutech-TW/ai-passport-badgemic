import Foundation
import Testing
@testable import BadgeMicCore

@Suite struct VCardContactTests {
    @Test func includesContactLines() {
        var b = BadgeInfo()
        b.name = "A"; b.phone = "+886 912;345"; b.email = "a@b.com"; b.website = "https://x.com/p?a=1,2"
        let lines = b.vCard().components(separatedBy: "\r\n")
        #expect(lines.contains("TEL;TYPE=CELL:+886 912\\;345"))
        #expect(lines.contains("EMAIL;TYPE=INTERNET:a@b.com"))
        #expect(lines.contains("URL:https://x.com/p?a=1\\,2"))
    }

    @Test func omitsEmptyContactLines() {
        var b = BadgeInfo(); b.name = "A"
        let v = b.vCard()
        #expect(!v.contains("TEL"))
        #expect(!v.contains("EMAIL"))
        #expect(!v.contains("URL"))
    }

    @Test func oldJSONDecodes() throws {
        let json = #"{"name":"Old","title":"T","organization":"O","extraLine":"x","qrMode":"vcard","customQR":""}"#
        let b = try JSONDecoder().decode(BadgeInfo.self, from: Data(json.utf8))
        #expect(b.name == "Old")
        #expect(b.phone == "" && b.email == "" && b.website == "")
        let again = try JSONDecoder().decode(BadgeInfo.self, from: JSONEncoder().encode(b))
        #expect(again == b)
    }
}

@Suite struct OffKeyTests {
    @Test func decodeKnownAndUnknown() throws {
        let d = try JSONDecoder().decode(DisplaySettings.self, from: Data(#"{"mode":"auto","off_key":"up_long"}"#.utf8))
        #expect(d.offKey == .upLong)
        let u = try JSONDecoder().decode(DisplaySettings.self, from: Data(#"{"mode":"auto","off_key":"weird"}"#.utf8))
        #expect(u.offKey == nil && u.mode == .auto)
        let a = try JSONDecoder().decode(DisplaySettings.self, from: Data(#"{"dim_s":30}"#.utf8))
        #expect(a.offKey == nil)
    }

    @Test func encodeUsesWireName() throws {
        let data = try JSONEncoder().encode(DisplaySettings(offKey: .downLong))
        let s = String(decoding: data, as: UTF8.self)
        #expect(s.contains(#""off_key":"down_long""#))
    }

    @Test func changesAndMerge() {
        let base = DisplaySettings.defaults
        #expect(base.offKey == OffKey.none)
        var draft = base
        draft.offKey = .upLong
        #expect(draft.changes(from: base) == DisplaySettings(offKey: .upLong))
        #expect(base.changes(from: base).isEmpty)
        #expect(base.merged(with: DisplaySettings(offKey: .downLong)).offKey == .downLong)
        #expect(base.merged(with: DisplaySettings()).offKey == OffKey.none)
    }
}

@Suite struct VirtualMicPickTests {
    typealias C = VirtualMic.Candidate
    @Test func badgeMicWinsOverBlackHole() {
        let devs = [C(uid: "a", name: "BlackHole 2ch"), C(uid: "BadgeMicLoopback_UID", name: "Whatever"), C(uid: "c", name: "MacBook Speakers")]
        #expect(VirtualMic.pick(devs) == 1)
        let byName = [C(uid: "a", name: "BlackHole 2ch"), C(uid: "b", name: "BadgeMic Microphone")]
        #expect(VirtualMic.pick(byName) == 1)
    }

    @Test func fallbackOrder() {
        let devs = [C(uid: "1", name: "Loopback Audio"), C(uid: "2", name: "BlackHole 16ch"), C(uid: "3", name: "Speakers")]
        #expect(VirtualMic.pick(devs) == 1)
        #expect(VirtualMic.pick([C(uid: "1", name: "VB-Cable"), C(uid: "2", name: "Soundflower (2ch)")]) == 1)
    }

    @Test func nothingFound() {
        #expect(VirtualMic.pick([C(uid: "1", name: "MacBook Pro Speakers"), C(uid: "2", name: "AirPods")]) == nil)
        #expect(VirtualMic.pick([]) == nil)
    }
}
