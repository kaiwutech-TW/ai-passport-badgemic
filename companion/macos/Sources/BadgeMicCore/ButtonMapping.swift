import Foundation

public enum ButtonAction: Codable, Equatable, Sendable {
    case none
    case keyShortcut(Hotkey)
    case returnKey
    case escape
    case typeText(String)
    case runShortcut(String)
    case openApp(String)
}

public struct ButtonSlot: Hashable, Sendable, CaseIterable {
    public var key: KeyName
    public var gesture: KeyGesture
    public init(_ key: KeyName, _ gesture: KeyGesture) { self.key = key; self.gesture = gesture }
    public var storageKey: String { "\(key.rawValue).\(gesture.rawValue)" }
    public static let allCases: [ButtonSlot] = [
        ButtonSlot(.up, .click), ButtonSlot(.up, .long), ButtonSlot(.down, .click), ButtonSlot(.down, .long),
    ]
}

/// UP/DOWN x click/long to action.
public struct ButtonMappings: Codable, Equatable, Sendable {
    public var table: [String: ButtonAction]

    public init(table: [String: ButtonAction] = [:]) { self.table = table }

    public subscript(slot: ButtonSlot) -> ButtonAction {
        get { table[slot.storageKey] ?? .none }
        set { table[slot.storageKey] = newValue }
    }

    public func action(for key: KeyName, _ gesture: KeyGesture) -> ButtonAction {
        self[ButtonSlot(key, gesture)]
    }
}
