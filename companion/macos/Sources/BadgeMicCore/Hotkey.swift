import Foundation

public struct KeyModifiers: OptionSet, Codable, Hashable, Sendable {
    public let rawValue: UInt8
    public init(rawValue: UInt8) { self.rawValue = rawValue }
    public static let command = KeyModifiers(rawValue: 1)
    public static let shift = KeyModifiers(rawValue: 2)
    public static let option = KeyModifiers(rawValue: 4)
    public static let control = KeyModifiers(rawValue: 8)
    public static let function = KeyModifiers(rawValue: 16)
}

/// A hotkey: Fn alone (key code 63, no modifiers) or any key code with modifiers.
public struct Hotkey: Codable, Hashable, Sendable {
    public static let fnKeyCode: UInt16 = 63
    public var keyCode: UInt16
    public var modifiers: KeyModifiers

    public init(keyCode: UInt16, modifiers: KeyModifiers = []) {
        self.keyCode = keyCode; self.modifiers = modifiers
    }

    public static let fn = Hotkey(keyCode: fnKeyCode)
    public static let returnKey = Hotkey(keyCode: 36)
    public static let escape = Hotkey(keyCode: 53)

    public var isFn: Bool { keyCode == Hotkey.fnKeyCode && modifiers.isEmpty }

    public var displayString: String {
        if isFn { return "Fn" }
        var s = ""
        if modifiers.contains(.control) { s += "⌃" }
        if modifiers.contains(.option) { s += "⌥" }
        if modifiers.contains(.shift) { s += "⇧" }
        if modifiers.contains(.command) { s += "⌘" }
        if modifiers.contains(.function) { s += "Fn+" }
        return s + Hotkey.keyName(keyCode)
    }

    private static let names: [UInt16: String] = [
        0: "A", 1: "S", 2: "D", 3: "F", 4: "H", 5: "G", 6: "Z", 7: "X", 8: "C", 9: "V", 11: "B", 12: "Q",
        13: "W", 14: "E", 15: "R", 16: "Y", 17: "T", 18: "1", 19: "2", 20: "3", 21: "4", 22: "6", 23: "5",
        24: "=", 25: "9", 26: "7", 27: "-", 28: "8", 29: "0", 30: "]", 31: "O", 32: "U", 33: "[", 34: "I",
        35: "P", 36: "↩", 37: "L", 38: "J", 39: "'", 40: "K", 41: ";", 42: "\\", 43: ",", 44: "/", 45: "N",
        46: "M", 47: ".", 48: "⇥", 49: "Space", 50: "`", 51: "⌫", 53: "⎋", 54: "Right ⌘", 55: "⌘",
        56: "⇧", 57: "⇪", 58: "⌥", 59: "⌃", 60: "Right ⇧", 61: "Right ⌥", 62: "Right ⌃", 63: "Fn",
        96: "F5", 97: "F6", 98: "F7", 99: "F3", 100: "F8", 101: "F9", 103: "F11", 105: "F13", 107: "F14",
        109: "F10", 111: "F12", 113: "F15", 118: "F4", 120: "F2", 122: "F1", 123: "←", 124: "→", 125: "↓", 126: "↑",
    ]

    public static func keyName(_ code: UInt16) -> String { names[code] ?? "Key \(code)" }
}
