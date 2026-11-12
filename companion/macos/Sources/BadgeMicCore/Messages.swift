import Foundation

public let supportedProtocolVersion = 1

public struct PageInfo: Codable, Equatable, Sendable {
    public var w: Int
    public var h: Int
    public var fmt: String
    public var count: Int
}

public struct HelloInfo: Equatable, Sendable {
    public var proto: Int
    public var fw: String
    public var model: String
    public var caps: [String]
    public var page: PageInfo?
    public var battery: Int
    public var display: DisplaySettings?
}

public enum KeyName: String, Codable, Sendable { case up, down }
public enum KeyGesture: String, Codable, Sendable { case click, long }

/// Device-to-Mac events (frame type EVENT).
public enum DeviceEvent: Equatable, Sendable {
    case hello(HelloInfo)
    case pttStart(id: Int)
    case pttEnd(id: Int, frames: Int, dropped: Int, reason: String?)
    case key(KeyName, KeyGesture)
    case display(DisplaySettings)
    case putReady(page: Int)
    case putDone(page: Int, ok: Bool, err: String?)
    case battery(Int)
    case error(cmd: String?, err: String?)
    case unknown(String)

    /// Parses an EVENT JSON payload. Returns nil for malformed JSON (the protocol says: log and ignore).
    public static func parse(_ json: [UInt8]) -> DeviceEvent? {
        guard let raw = try? JSONDecoder().decode(Raw.self, from: Data(json)) else { return nil }
        switch raw.ev {
        case "hello":
            return .hello(HelloInfo(proto: raw.proto ?? 0, fw: raw.fw ?? "?", model: raw.model ?? "?",
                                    caps: raw.caps ?? [], page: raw.page, battery: raw.battery ?? -1,
                                    display: raw.display))
        case "ptt_start":
            guard let id = raw.id else { return nil }
            return .pttStart(id: id)
        case "ptt_end":
            guard let id = raw.id else { return nil }
            return .pttEnd(id: id, frames: raw.frames ?? 0, dropped: raw.dropped ?? 0, reason: raw.reason)
        case "key":
            guard let k = raw.key.flatMap(KeyName.init), let g = raw.gesture.flatMap(KeyGesture.init) else { return nil }
            return .key(k, g)
        case "display":
            // The display fields sit at the top level of the event object.
            guard let d = try? JSONDecoder().decode(DisplaySettings.self, from: Data(json)) else { return nil }
            return .display(d)
        case "put_ready":
            return .putReady(page: raw.page_ ?? 0)
        case "put_done":
            return .putDone(page: raw.page_ ?? 0, ok: raw.ok ?? false, err: raw.err)
        case "battery":
            return .battery(raw.battery ?? -1)
        case "error":
            return .error(cmd: raw.cmd, err: raw.err)
        default:
            return .unknown(raw.ev)
        }
    }

    private struct Raw: Decodable {
        var ev: String
        var proto: Int?
        var fw: String?
        var model: String?
        var caps: [String]?
        var page: PageInfo?
        var page_: Int?
        var battery: Int?
        var display: DisplaySettings?
        var id: Int?
        var frames: Int?
        var dropped: Int?
        var reason: String?
        var key: String?
        var gesture: String?
        var ok: Bool?
        var err: String?
        var cmd: String?

        enum CodingKeys: String, CodingKey {
            case ev, proto, fw, model, caps, page, battery, display, id, frames, dropped, reason, key, gesture, ok, err, cmd
        }

        init(from decoder: Decoder) throws {
            let c = try decoder.container(keyedBy: CodingKeys.self)
            ev = try c.decode(String.self, forKey: .ev)
            proto = try? c.decodeIfPresent(Int.self, forKey: .proto)
            fw = try? c.decodeIfPresent(String.self, forKey: .fw)
            model = try? c.decodeIfPresent(String.self, forKey: .model)
            caps = try? c.decodeIfPresent([String].self, forKey: .caps)
            // `page` is an object in hello and an integer in put_ready/put_done.
            if let p = try? c.decodeIfPresent(PageInfo.self, forKey: .page) { page = p }
            if let n = try? c.decodeIfPresent(Int.self, forKey: .page) { page_ = n }
            battery = try? c.decodeIfPresent(Int.self, forKey: .battery)
            display = try? c.decodeIfPresent(DisplaySettings.self, forKey: .display)
            id = try? c.decodeIfPresent(Int.self, forKey: .id)
            frames = try? c.decodeIfPresent(Int.self, forKey: .frames)
            dropped = try? c.decodeIfPresent(Int.self, forKey: .dropped)
            reason = try? c.decodeIfPresent(String.self, forKey: .reason)
            key = try? c.decodeIfPresent(String.self, forKey: .key)
            gesture = try? c.decodeIfPresent(String.self, forKey: .gesture)
            ok = try? c.decodeIfPresent(Bool.self, forKey: .ok)
            err = try? c.decodeIfPresent(String.self, forKey: .err)
            cmd = try? c.decodeIfPresent(String.self, forKey: .cmd)
        }
    }
}

/// Mac-to-device commands (frame type CONTROL).
public struct ControlCommand: Codable, Equatable, Sendable {
    public var cmd: String
    public var proto: Int?
    public var app: String?
    public var ver: String?
    public var id: Int?
    public var page: Int?
    public var size: Int?
    public var crc32: UInt32?
    // set_display fields
    public var mode: DisplayMode?
    public var dim_s: Int?
    public var off_s: Int?
    public var sleep_min: Int?
    public var brightness: Int?
    public var dim_brightness: Int?

    public init(cmd: String) { self.cmd = cmd }

    public static func hello(app: String = "BadgeMic", version: String) -> ControlCommand {
        var c = ControlCommand(cmd: "hello"); c.proto = supportedProtocolVersion; c.app = app; c.ver = version; return c
    }
    public static func pttAck(id: Int) -> ControlCommand { var c = ControlCommand(cmd: "ptt_ack"); c.id = id; return c }
    public static func putBegin(page: Int, size: Int, crc32: UInt32) -> ControlCommand {
        var c = ControlCommand(cmd: "put_begin"); c.page = page; c.size = size; c.crc32 = crc32; return c
    }
    public static func putEnd(page: Int) -> ControlCommand { var c = ControlCommand(cmd: "put_end"); c.page = page; return c }
    public static func clearPage(_ page: Int) -> ControlCommand { var c = ControlCommand(cmd: "clear_page"); c.page = page; return c }
    public static func setDisplay(_ s: DisplaySettings) -> ControlCommand {
        var c = ControlCommand(cmd: "set_display")
        c.mode = s.mode; c.dim_s = s.dimS; c.off_s = s.offS; c.sleep_min = s.sleepMin
        c.brightness = s.brightness; c.dim_brightness = s.dimBrightness
        return c
    }

    public func jsonBytes() throws -> [UInt8] {
        let enc = JSONEncoder()
        enc.outputFormatting = [.sortedKeys, .withoutEscapingSlashes]
        return [UInt8](try enc.encode(self))
    }

    /// The complete wire frame for this command.
    public func frame() throws -> [UInt8] {
        try FrameCodec.encode(type: .control, payload: try jsonBytes())
    }
}
