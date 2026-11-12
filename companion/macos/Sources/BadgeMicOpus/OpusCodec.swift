import COpus
import Foundation

public enum OpusError: Error, Equatable {
    case createFailed(Int32)
    case decodeFailed(Int32)
    case encodeFailed(Int32)
}

/// Mono Opus decoder. The device sends 16 kHz / 60 ms packets (960 samples).
public final class OpusDecoder {
    public static let sampleRate = 16_000
    public static let frameSamples = 960
    private var decoder: OpaquePointer?
    private var buffer = [Float](repeating: 0, count: 5760)

    public init(sampleRate: Int = OpusDecoder.sampleRate) throws {
        var err: Int32 = 0
        decoder = opus_decoder_create(Int32(sampleRate), 1, &err)
        if err != OPUS_OK || decoder == nil { throw OpusError.createFailed(err) }
    }

    deinit { if let decoder { opus_decoder_destroy(decoder) } }

    /// Decodes one packet into float samples in [-1, 1].
    public func decode(_ packet: [UInt8]) throws -> [Float] {
        let n = packet.withUnsafeBufferPointer { p in
            opus_decode_float(decoder!, p.baseAddress, Int32(p.count), &buffer, Int32(buffer.count), 0)
        }
        if n < 0 { throw OpusError.decodeFailed(n) }
        return Array(buffer[0..<Int(n)])
    }

    /// Packet-loss concealment for one missing frame.
    public func conceal(samples: Int = OpusDecoder.frameSamples) throws -> [Float] {
        let n = opus_decode_float(decoder!, nil, 0, &buffer, Int32(samples), 0)
        if n < 0 { throw OpusError.decodeFailed(n) }
        return Array(buffer[0..<Int(n)])
    }

    public func reset() {
        // Recreate state by decoding nothing: OPUS_RESET_STATE is a variadic ctl, so just rebuild.
        if let decoder { opus_decoder_destroy(decoder) }
        var err: Int32 = 0
        decoder = opus_decoder_create(Int32(Self.sampleRate), 1, &err)
    }
}

/// Mono Opus encoder, used by tests and tooling to simulate the device.
public final class OpusEncoder {
    private var encoder: OpaquePointer?

    public init(sampleRate: Int = 16_000, dtx: Bool = true, complexity: Int = 0) throws {
        var err: Int32 = 0
        encoder = opus_encoder_create(Int32(sampleRate), 1, OPUS_APPLICATION_VOIP, &err)
        if err != OPUS_OK || encoder == nil { throw OpusError.createFailed(err) }
        _ = bm_opus_set_dtx(encoder, dtx ? 1 : 0)
        _ = bm_opus_set_complexity(encoder, Int32(complexity))
    }

    deinit { if let encoder { opus_encoder_destroy(encoder) } }

    public func encode(_ pcm: [Int16]) throws -> [UInt8] {
        var out = [UInt8](repeating: 0, count: 512)
        let n = pcm.withUnsafeBufferPointer { p in
            opus_encode(encoder!, p.baseAddress!, Int32(p.count), &out, Int32(out.count))
        }
        if n < 0 { throw OpusError.encodeFailed(n) }
        return Array(out[0..<Int(n)])
    }
}
