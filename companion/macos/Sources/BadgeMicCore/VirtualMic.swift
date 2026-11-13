import Foundation

/// Picks the virtual microphone output device. Pure so it can be unit-tested.
public enum VirtualMic {
    public static let badgeMicUID = "BadgeMicLoopback_UID"
    /// Name fragments tried in order after the BadgeMic driver itself.
    public static let fallbackNames = ["blackhole", "miremotev", "loopback", "soundflower", "vb-cable"]

    public struct Candidate: Equatable, Sendable {
        public var uid: String
        public var name: String
        public init(uid: String, name: String) { self.uid = uid; self.name = name }
    }

    /// Index of the best candidate, or nil when none looks like a virtual mic.
    /// Priority: BadgeMic driver (UID or name), then `fallbackNames` in order; ties keep list order.
    public static func pick(_ devices: [Candidate]) -> Int? {
        if let i = devices.firstIndex(where: { $0.uid == badgeMicUID }) { return i }
        if let i = devices.firstIndex(where: { $0.name.localizedCaseInsensitiveContains("badgemic") }) { return i }
        for frag in fallbackNames {
            if let i = devices.firstIndex(where: { $0.name.localizedCaseInsensitiveContains(frag) }) { return i }
        }
        return nil
    }
}
