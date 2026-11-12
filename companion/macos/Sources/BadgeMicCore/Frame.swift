import Foundation

/// Frame types of the Badge Mic wire protocol (see docs/development/engineering/badge-mic-protocol.md).
public enum FrameType: UInt8, Sendable, CaseIterable {
    case control = 0x03   // Mac -> device, JSON
    case event = 0x04     // device -> Mac, JSON
    case audioOpus = 0x05 // device -> Mac, [SEQ] + Opus packet
    case putData = 0x07   // Mac -> device, [OFFSET:4 BE] + bytes

    public var maxPayload: Int {
        switch self {
        case .control, .event: return 512
        case .audioOpus: return 1 + 512
        case .putData: return 4 + 224
        }
    }

    /// Smallest legal payload (a SEQ byte / an offset).
    public var minPayload: Int {
        switch self {
        case .control, .event: return 0
        case .audioOpus: return 1
        case .putData: return 4
        }
    }
}

public struct Frame: Equatable, Sendable {
    public var type: FrameType
    public var flags: UInt8
    public var payload: [UInt8]

    public init(type: FrameType, flags: UInt8 = 0, payload: [UInt8]) {
        self.type = type
        self.flags = flags
        self.payload = payload
    }
}

public enum FrameError: Error, Equatable {
    case payloadTooLarge(Int)
}

public enum FrameCodec {
    public static let magic0: UInt8 = 0xA5
    public static let magic1: UInt8 = 0x5A
    public static let headerSize = 6

    public static func encode(_ frame: Frame) throws -> [UInt8] {
        guard frame.payload.count <= frame.type.maxPayload else {
            throw FrameError.payloadTooLarge(frame.payload.count)
        }
        var out = [magic0, magic1, frame.type.rawValue, frame.flags,
                   UInt8(frame.payload.count >> 8), UInt8(frame.payload.count & 0xFF)]
        out.append(contentsOf: frame.payload)
        return out
    }

    public static func encode(type: FrameType, payload: [UInt8]) throws -> [UInt8] {
        try encode(Frame(type: type, payload: payload))
    }
}

/// Byte-wise resynchronising frame parser.
///
/// A header is accepted only if it has the magic, a known type and a legal length.
/// If a new valid header shows up inside the span of a candidate frame, the candidate is
/// discarded (the sender was cut off) and parsing resumes at the new header.
public struct FrameReassembler {
    private var buffer: [UInt8] = []
    public private(set) var skippedBytes = 0
    public private(set) var resyncCount = 0

    public init() {}

    private enum Probe {
        case valid(FrameType, Int)
        case invalid
        case needMore
    }

    private func probe(_ i: Int) -> Probe {
        guard i < buffer.count else { return .needMore }
        if buffer[i] != FrameCodec.magic0 { return .invalid }
        if i + 1 >= buffer.count { return .needMore }
        if buffer[i + 1] != FrameCodec.magic1 { return .invalid }
        if i + FrameCodec.headerSize > buffer.count { return .needMore }
        guard let type = FrameType(rawValue: buffer[i + 2]) else { return .invalid }
        let len = Int(buffer[i + 4]) << 8 | Int(buffer[i + 5])
        guard len >= type.minPayload, len <= type.maxPayload else { return .invalid }
        return .valid(type, len)
    }

    public mutating func reset() { buffer.removeAll() }

    public mutating func feed(_ bytes: [UInt8]) -> [Frame] {
        buffer.append(contentsOf: bytes)
        var frames: [Frame] = []
        var i = 0
        parse: while i < buffer.count {
            switch probe(i) {
            case .invalid:
                i += 1
                skippedBytes += 1
            case .needMore:
                break parse
            case .valid(let type, let len):
                let total = FrameCodec.headerSize + len
                // Look for a new valid header inside the candidate's span.
                let spanEnd = min(buffer.count, i + total)
                var embedded: Int?
                var j = i + 1
                while j < spanEnd {
                    if case .valid = probe(j) { embedded = j; break }
                    j += 1
                }
                if let j = embedded {
                    skippedBytes += j - i
                    resyncCount += 1
                    i = j
                    continue
                }
                if buffer.count < i + total { break parse } // wait for the rest
                let start = i + FrameCodec.headerSize
                frames.append(Frame(type: type, flags: buffer[i + 3], payload: Array(buffer[start..<(i + total)])))
                i += total
            }
        }
        buffer.removeFirst(i)
        return frames
    }

    public mutating func feed(_ data: Data) -> [Frame] { feed([UInt8](data)) }
}
