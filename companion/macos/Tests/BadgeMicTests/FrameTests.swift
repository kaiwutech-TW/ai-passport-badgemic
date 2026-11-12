import Testing
@testable import BadgeMicCore

private func enc(_ t: FrameType, _ p: [UInt8]) -> [UInt8] { try! FrameCodec.encode(type: t, payload: p) }

@Suite struct FrameTests {
    @Test func encodeLayout() {
        let b = enc(.event, [1, 2, 3])
        #expect(b == [0xA5, 0x5A, 0x04, 0x00, 0x00, 0x03, 1, 2, 3])
    }

    @Test func payloadLimits() {
        #expect(throws: FrameError.self) { try FrameCodec.encode(type: .putData, payload: [UInt8](repeating: 0, count: 229)) }
        #expect((try? FrameCodec.encode(type: .putData, payload: [UInt8](repeating: 0, count: 228))) != nil)
    }

    @Test func singleFrame() {
        var r = FrameReassembler()
        let f = r.feed(enc(.control, Array("{}".utf8)))
        #expect(f == [Frame(type: .control, payload: Array("{}".utf8))])
    }

    @Test func splitAcrossNotifications() {
        var r = FrameReassembler()
        let bytes = enc(.audioOpus, [7] + [UInt8](repeating: 0x42, count: 100))
        var got: [Frame] = []
        for b in bytes { got += r.feed([b]) }   // worst case: one byte at a time
        #expect(got.count == 1)
        #expect(got[0].payload.first == 7 && got[0].payload.count == 101)
        var r2 = FrameReassembler()
        #expect(r2.feed(Array(bytes[0..<3])).isEmpty)
        #expect(r2.feed(Array(bytes[3..<50])).isEmpty)
        #expect(r2.feed(Array(bytes[50...])).count == 1)
    }

    @Test func multipleFramesPerChunk() {
        var r = FrameReassembler()
        let a = enc(.event, Array("a".utf8)), b = enc(.audioOpus, [1, 2, 3]), c = enc(.event, Array("c".utf8))
        let f = r.feed(a + b + c)
        #expect(f.map(\.type) == [.event, .audioOpus, .event])
        // Plus a trailing partial frame that completes later.
        var r2 = FrameReassembler()
        #expect(r2.feed(a + Array(b[0..<4])).count == 1)
        #expect(r2.feed(Array(b[4...])).count == 1)
    }

    @Test func garbageAndResync() {
        var r = FrameReassembler()
        let good = enc(.event, Array("ok".utf8))
        let garbage: [UInt8] = [0x00, 0xA5, 0x11, 0xA5, 0x5A, 0x99, 0x00, 0x00, 0xA5, 0x5A, 0x04, 0x00, 0xFF, 0xFF, 0x01]
        let f = r.feed(garbage + good)
        #expect(f.count == 1)
        #expect(f[0].payload == Array("ok".utf8))
        #expect(r.skippedBytes >= garbage.count)
    }

    @Test func insertedHeaderMidFrame() {
        var r = FrameReassembler()
        let broken = Array(enc(.event, [UInt8](repeating: 0x41, count: 100))[0..<40]) // cut off
        let good = enc(.event, Array("fresh".utf8))
        let f = r.feed(broken + good)
        #expect(f.count == 1)
        #expect(f[0].payload == Array("fresh".utf8))
        #expect(r.resyncCount == 1)
        // Same when the new header arrives in a later notification.
        var r2 = FrameReassembler()
        #expect(r2.feed(broken).isEmpty)
        #expect(r2.feed(good).count == 1)
    }

    @Test func unknownTypeSkipped() {
        var r = FrameReassembler()
        let f = r.feed([0xA5, 0x5A, 0x09, 0, 0, 1, 0xEE] + enc(.event, [1]))
        #expect(f.count == 1)
    }
}
