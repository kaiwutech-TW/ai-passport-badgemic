import Foundation

public let supportedProtocolVersion = 1

public struct PageInfo: Codable, Equatable, Sendable {
    public var w: Int
    public var h: Int
    public var fmt: String
    public var count: Int
}

/// Diagnostics attached to `ptt_end` (all fields optional; unknown keys are ignored).
public struct PttStats: Codable, Equatable, Sendable {
    public var encFirstUs: Int?
    public var encAvgUs: Int?
    public var encMaxUs: Int?
    public var stackFree: Int?
    public var readErr: Int?
    public var clip: Int?

    enum CodingKeys: String, CodingKey {
        case encFirstUs = "enc_first_us", encAvgUs = "enc_avg_us", encMaxUs = "enc_max_us"
        case stackFree = "stack_free", readErr = "read_err", clip
    }

    public init(encFirstUs: Int? = nil, encAvgUs: Int? = nil, encMaxUs: Int? = nil,
                stackFree: Int? = nil, readErr: Int? = nil, clip: Int? = nil) {
        self.encFirstUs = encFirstUs; self.encAvgUs = encAvgUs; self.encMaxUs = encMaxUs
        self.stackFree = stackFree; self.readErr = readErr; self.clip = clip
    }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        // A wrongly typed field is treated as absent rather than failing the whole event.
        encFirstUs = try? c.decodeIfPresent(Int.self, forKey: .encFirstUs)
        encAvgUs = try? c.decodeIfPresent(Int.self, forKey: .encAvgUs)
        encMaxUs = try? c.decodeIfPresent(Int.self, forKey: .encMaxUs)
        stackFree = try? c.decodeIfPresent(Int.self, forKey: .stackFree)
        readErr = try? c.decodeIfPresent(Int.self, forKey: .readErr)
        clip = try? c.decodeIfPresent(Int.self, forKey: .clip)
    }
}

public let micGainStepsDb = [0, 6, 12, 18, 24, 30, 36, 42]

public struct HelloInfo: Equatable, Sendable {
    public var proto: Int
    public var fw: String
    public var model: String
    public var caps: [String]
    public var page: PageInfo?
    public var battery: Int
    public var display: DisplaySettings?
    /// nil when the firmware has no `audio` object (older firmware).
    public var micGainDb: Int?
}

public enum KeyName: String, Codable, Sendable { case up, down }
public enum KeyGesture: String, Codable, Sendable { case click, long }

/// Device-to-Mac events (frame type EVENT).
public enum DeviceEvent: Equatable, Sendable {
    case hello(HelloInfo)
    case pttStart(id: Int)
    case pttEnd(id: Int, frames: Int, dropped: Int, reason: String?, stats: PttStats? = nil)
    case audio(micGainDb: Int)
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
                                    display: raw.display, micGainDb: raw.audio?.mic_gain_db))
        case "ptt_start":
            guard let id = raw.id else { return nil }
            return .pttStart(id: id)
        case "ptt_end":
            guard let id = raw.id else { return nil }
            return .pttEnd(id: id, frames: raw.frames ?? 0, dropped: raw.dropped ?? 0, reason: raw.reason, stats: raw.stats)
        case "audio":
            guard let g = raw.mic_gain_db else { return nil }
            return .audio(micGainDb: g)
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
        var stats: PttStats?
        var audio: AudioObj?
        var mic_gain_db: Int?

        struct AudioObj: Decodable {
            var mic_gain_db: Int?
            init(from decoder: Decoder) throws {
                let c = try decoder.container(keyedBy: K.self)
                mic_gain_db = try? c.decodeIfPresent(Int.self, forKey: .mic_gain_db)
            }
            enum K: String, CodingKey { case mic_gain_db }
        }

        enum CodingKeys: String, CodingKey {
            case ev, proto, fw, model, caps, page, battery, display, id, frames, dropped, reason, key, gesture, ok, err, cmd, stats, audio, mic_gain_db
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
            stats = try? c.decodeIfPresent(PttStats.self, forKey: .stats)
            audio = try? c.decodeIfPresent(AudioObj.self, forKey: .audio)
            mic_gain_db = try? c.decodeIfPresent(Int.self, forKey: .mic_gain_db)
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
    // set_audio field
    public var mic_gain_db: Int?

    public init(cmd: String) { self.cmd = cmd }

    public static func hello(app: String = "BadgeMic", version: String) -> ControlCommand {
        var c = ControlCommand(cmd: "hello"); c.proto = supportedProtocolVersion; c.app = app; c.ver = version; return c
    }
    /// Asks the device to resend its hello (sent when the subscription-time hello was missed).
    public static func getHello() -> ControlCommand { ControlCommand(cmd: "get_hello") }
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

    public static func setAudio(micGainDb: Int) -> ControlCommand {
        var c = ControlCommand(cmd: "set_audio"); c.mic_gain_db = micGainDb; return c
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
