import Foundation
import Testing
import BadgeMicCore
import BadgeMicOpus

@Suite struct OpusEndToEndTests {
    @Test func toneThroughFramesReassemblerAndDecoder() throws {
        let frameSamples = 960
        let frameCount = 17 // 1.02 s
        let hz = 440.0
        let pcm: [Int16] = (0..<(frameCount * frameSamples)).map { i in
            Int16(12_000 * sin(2 * Double.pi * hz * Double(i) / 16_000))
        }

        // Device side: encode and frame (SEQ + packet), plus a ptt_start event first.
        let encoder = try OpusEncoder(sampleRate: 16_000, dtx: true, complexity: 0)
        var wire: [UInt8] = try FrameCodec.encode(type: .event, payload: Array(#"{"ev":"ptt_start","id":1}"#.utf8))
        for n in 0..<frameCount {
            let packet = try encoder.encode(Array(pcm[(n * frameSamples)..<((n + 1) * frameSamples)]))
            wire += try FrameCodec.encode(type: .audioOpus, payload: [UInt8(n & 0xFF)] + packet)
        }
        wire += try FrameCodec.encode(type: .event, payload: Array(#"{"ev":"ptt_end","id":1,"frames":17,"dropped":0}"#.utf8))

        // Mac side: arbitrary 20-byte notifications -> reassembler -> decoder.
        var reassembler = FrameReassembler()
        let decoder = try OpusDecoder()
        var seq = SeqTracker()
        var decoded: [Float] = []
        var events: [DeviceEvent] = []
        var i = 0
        while i < wire.count {
            let end = min(i + 20, wire.count)
            for frame in reassembler.feed(Array(wire[i..<end])) {
                switch frame.type {
                case .audioOpus:
                    #expect(seq.observe(frame.payload[0]) == 0)
                    decoded += try decoder.decode(Array(frame.payload.dropFirst()))
                case .event:
                    if let e = DeviceEvent.parse(frame.payload) { events.append(e) }
                default: break
                }
            }
            i = end
        }

        #expect(events == [.pttStart(id: 1), .pttEnd(id: 1, frames: 17, dropped: 0, reason: nil)])
        #expect(seq.received == frameCount && seq.lost == 0)
        #expect(decoded.count == frameCount * frameSamples)

        // Non-silent and roughly the right pitch (skip the codec's start-up).
        let body = Array(decoded[2000..<(decoded.count - 2000)])
        let rms = (body.reduce(0) { $0 + Double($1 * $1) } / Double(body.count)).squareRoot()
        #expect(rms > 0.1)
        var crossings = 0
        for k in 1..<body.count where (body[k - 1] < 0) != (body[k] < 0) { crossings += 1 }
        let seconds = Double(body.count) / 16_000
        let estimatedHz = Double(crossings) / 2 / seconds
        #expect(abs(estimatedHz - hz) < 30)
    }

    @Test func concealmentProducesOneFrame() throws {
        let enc = try OpusEncoder()
        let dec = try OpusDecoder()
        let pcm = (0..<960).map { Int16(8000 * sin(2 * Double.pi * 300 * Double($0) / 16_000)) }
        _ = try dec.decode(try enc.encode(pcm))
        #expect(try dec.conceal().count == 960)
    }
}
