import Foundation

/// How a dictation tool is triggered while the badge's OK button is held.
public struct TriggerProfile: Codable, Equatable, Identifiable, Sendable {
    public var id: UUID
    public var name: String
    public var hotkey: Hotkey
    public var behavior: PttBehavior
    public var leadMs: Int
    public var tailMs: Int
    /// Temporarily make the virtual mic the system default input during a turn.
    public var switchSystemInput: Bool
    /// CoreAudio UID of the output device that feeds the virtual microphone.
    public var outputDeviceUID: String?

    public init(id: UUID = UUID(), name: String, hotkey: Hotkey = .fn, behavior: PttBehavior = .hold,
                leadMs: Int = 200, tailMs: Int = 300, switchSystemInput: Bool = false,
                outputDeviceUID: String? = nil) {
        self.id = id; self.name = name; self.hotkey = hotkey; self.behavior = behavior
        self.leadMs = leadMs; self.tailMs = tailMs
        self.switchSystemInput = switchSystemInput; self.outputDeviceUID = outputDeviceUID
    }

    public var pttConfig: PttConfig { PttConfig(behavior: behavior, leadMs: leadMs, tailMs: tailMs) }

    public static let parrotingID = UUID(uuidString: "5B1D0001-0000-4000-8000-000000000001")!
    public static let doubaoID = UUID(uuidString: "5B1D0001-0000-4000-8000-000000000002")!
    public static let customID = UUID(uuidString: "5B1D0001-0000-4000-8000-000000000003")!

    public static var presets: [TriggerProfile] {
        [
            TriggerProfile(id: parrotingID, name: "學舌鳥 Parroting", hotkey: .fn, behavior: .hold),
            TriggerProfile(id: doubaoID, name: "豆包 Doubao", hotkey: .fn, behavior: .hold),
            TriggerProfile(id: customID, name: "Custom", hotkey: Hotkey(keyCode: 49, modifiers: [.option]), behavior: .toggle),
        ]
    }
}

public struct ProfileSettings: Codable, Equatable, Sendable {
    public var profiles: [TriggerProfile]
    public var selectedID: UUID

    public init(profiles: [TriggerProfile], selectedID: UUID) {
        self.profiles = profiles; self.selectedID = selectedID
    }

    public static var defaults: ProfileSettings {
        ProfileSettings(profiles: TriggerProfile.presets, selectedID: TriggerProfile.parrotingID)
    }

    public var selected: TriggerProfile {
        profiles.first { $0.id == selectedID } ?? profiles.first ?? TriggerProfile.presets[0]
    }
}

/// JSON persistence codec shared by UserDefaults storage and tests.
public enum SettingsCodec {
    static var encoder: JSONEncoder {
        let e = JSONEncoder(); e.outputFormatting = [.sortedKeys, .prettyPrinted]; return e
    }

    public static func encode<T: Encodable>(_ value: T) throws -> Data { try encoder.encode(value) }

    /// Decodes; on empty/corrupt data or an empty profile list, falls back to `fallback`.
    public static func decodeProfiles(_ data: Data?, fallback: ProfileSettings = .defaults) -> ProfileSettings {
        guard let data, let s = try? JSONDecoder().decode(ProfileSettings.self, from: data), !s.profiles.isEmpty else {
            return fallback
        }
        var fixed = s
        if !fixed.profiles.contains(where: { $0.id == fixed.selectedID }) { fixed.selectedID = fixed.profiles[0].id }
        return fixed
    }

    public static func decodeMappings(_ data: Data?) -> ButtonMappings {
        guard let data, let m = try? JSONDecoder().decode(ButtonMappings.self, from: data) else { return ButtonMappings() }
        return m
    }

    public static func decodeBadge(_ data: Data?) -> BadgeInfo {
        guard let data, let b = try? JSONDecoder().decode(BadgeInfo.self, from: data) else { return BadgeInfo() }
        return b
    }
}
