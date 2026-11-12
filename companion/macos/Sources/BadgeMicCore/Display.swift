import Foundation

public enum DisplayMode: String, Codable, Sendable, CaseIterable {
    case auto
    case alwaysOn = "always_on"
}

/// Display settings as used in `set_display`, the `hello.display` object and `display` events.
/// Every field is optional: in a command, omitted fields keep the device's current value.
public struct DisplaySettings: Codable, Equatable, Sendable {
    public var mode: DisplayMode?
    public var dimS: Int?
    public var offS: Int?
    public var sleepMin: Int?
    public var brightness: Int?
    public var dimBrightness: Int?

    enum CodingKeys: String, CodingKey {
        case mode
        case dimS = "dim_s"
        case offS = "off_s"
        case sleepMin = "sleep_min"
        case brightness
        case dimBrightness = "dim_brightness"
    }

    public init(mode: DisplayMode? = nil, dimS: Int? = nil, offS: Int? = nil, sleepMin: Int? = nil,
                brightness: Int? = nil, dimBrightness: Int? = nil) {
        self.mode = mode; self.dimS = dimS; self.offS = offS; self.sleepMin = sleepMin
        self.brightness = brightness; self.dimBrightness = dimBrightness
    }

    public static let defaults = DisplaySettings(mode: .auto, dimS: 30, offS: 120, sleepMin: 30,
                                                 brightness: 80, dimBrightness: 20)

    /// Decodes either the object form or the legacy plain-string form (`"auto"`).
    public init(from decoder: Decoder) throws {
        if let single = try? decoder.singleValueContainer(), let s = try? single.decode(String.self) {
            self.init(mode: DisplayMode(rawValue: s))
            return
        }
        let c = try decoder.container(keyedBy: CodingKeys.self)
        // An unknown mode string must not break the whole hello.
        let modeString = try? c.decodeIfPresent(String.self, forKey: .mode)
        self.init(mode: modeString.flatMap { DisplayMode(rawValue: $0) },
                  dimS: try c.decodeIfPresent(Int.self, forKey: .dimS),
                  offS: try c.decodeIfPresent(Int.self, forKey: .offS),
                  sleepMin: try c.decodeIfPresent(Int.self, forKey: .sleepMin),
                  brightness: try c.decodeIfPresent(Int.self, forKey: .brightness),
                  dimBrightness: try c.decodeIfPresent(Int.self, forKey: .dimBrightness))
    }

    /// Returns a copy with fields from `other` overriding where present.
    public func merged(with other: DisplaySettings) -> DisplaySettings {
        DisplaySettings(mode: other.mode ?? mode, dimS: other.dimS ?? dimS, offS: other.offS ?? offS,
                        sleepMin: other.sleepMin ?? sleepMin, brightness: other.brightness ?? brightness,
                        dimBrightness: other.dimBrightness ?? dimBrightness)
    }

    /// Fields of `self` that differ from `base` (only those are sent to the device).
    public func changes(from base: DisplaySettings) -> DisplaySettings {
        func d<T: Equatable>(_ a: T?, _ b: T?) -> T? { a == b ? nil : a }
        return DisplaySettings(mode: d(mode, base.mode), dimS: d(dimS, base.dimS), offS: d(offS, base.offS),
                               sleepMin: d(sleepMin, base.sleepMin), brightness: d(brightness, base.brightness),
                               dimBrightness: d(dimBrightness, base.dimBrightness))
    }

    public var isEmpty: Bool { self == DisplaySettings() }

    /// Validation of the documented ranges. Returns a human-readable problem or nil.
    public func validationError() -> String? {
        if let v = dimS, !(5...600).contains(v) { return "dim_s must be 5-600" }
        if let v = offS {
            if !(5...3600).contains(v) { return "off_s must be 5-3600" }
            if let d = dimS, v < d { return "off_s must be >= dim_s" }
        }
        if let v = sleepMin, !(v == 0 || (5...240).contains(v)) { return "sleep_min must be 0 or 5-240" }
        if let v = brightness, !(10...100).contains(v) { return "brightness must be 10-100" }
        if let v = dimBrightness {
            if !(5...100).contains(v) { return "dim_brightness must be 5-100" }
            if let b = brightness, v > b { return "dim_brightness must be <= brightness" }
        }
        return nil
    }

    /// Clamps to documented ranges, keeping cross-field constraints.
    public func clamped() -> DisplaySettings {
        var r = self
        r.dimS = dimS.map { min(max($0, 5), 600) }
        r.offS = offS.map { min(max($0, r.dimS ?? 5), 3600) }
        r.sleepMin = sleepMin.map { $0 == 0 ? 0 : min(max($0, 5), 240) }
        r.brightness = brightness.map { min(max($0, 10), 100) }
        r.dimBrightness = dimBrightness.map { min(max($0, 5), r.brightness ?? 100) }
        return r
    }
}
