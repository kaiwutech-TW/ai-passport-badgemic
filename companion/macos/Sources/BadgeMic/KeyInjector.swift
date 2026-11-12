import AppKit
import ApplicationServices
import BadgeMicCore
import CoreGraphics

/// Posts synthetic keyboard events. Needs the Accessibility permission to take effect.
/// Written from scratch for this project (clean-room; no GPL code).
final class KeyInjector {
    private let source = CGEventSource(stateID: .hidSystemState)
    /// The hotkey currently held down by `press`, if any. Used as a last-resort failsafe.
    private(set) var held: Hotkey?

    static func isTrusted(prompt: Bool) -> Bool {
        let key = kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String
        return AXIsProcessTrustedWithOptions([key: prompt] as CFDictionary)
    }

    static func cgFlags(_ m: KeyModifiers) -> CGEventFlags {
        var f: CGEventFlags = []
        if m.contains(.command) { f.insert(.maskCommand) }
        if m.contains(.shift) { f.insert(.maskShift) }
        if m.contains(.option) { f.insert(.maskAlternate) }
        if m.contains(.control) { f.insert(.maskControl) }
        if m.contains(.function) { f.insert(.maskSecondaryFn) }
        return f
    }

    private func post(_ hotkey: Hotkey, down: Bool) {
        if hotkey.isFn {
            // The Fn/Globe key is a modifier: it is reported as a flagsChanged event, not keyDown/keyUp.
            guard let e = CGEvent(keyboardEventSource: source, virtualKey: CGKeyCode(Hotkey.fnKeyCode), keyDown: down) else { return }
            e.type = .flagsChanged
            e.flags = down ? .maskSecondaryFn : []
            e.post(tap: .cghidEventTap)
        } else {
            guard let e = CGEvent(keyboardEventSource: source, virtualKey: CGKeyCode(hotkey.keyCode), keyDown: down) else { return }
            e.flags = down ? Self.cgFlags(hotkey.modifiers) : []
            if !down { e.flags = Self.cgFlags(hotkey.modifiers) }
            e.post(tap: .cghidEventTap)
        }
    }

    func press(_ hotkey: Hotkey) {
        if let h = held, h != hotkey { post(h, down: false) }
        held = hotkey
        post(hotkey, down: true)
    }

    func release() {
        guard let h = held else { return }
        held = nil
        post(h, down: false)
    }

    /// Press and release (used by toggle profiles and button shortcuts).
    func tap(_ hotkey: Hotkey) {
        post(hotkey, down: true)
        usleep(30_000)
        post(hotkey, down: false)
    }

    /// Failsafe: never leave a key down.
    func releaseIfHeld() { release() }

    /// Types text by placing it on the pasteboard and pressing Cmd-V, then restores the clipboard.
    func typeText(_ text: String) {
        let pb = NSPasteboard.general
        let saved: [[(NSPasteboard.PasteboardType, Data)]] = (pb.pasteboardItems ?? []).map { item in
            item.types.compactMap { t in item.data(forType: t).map { (t, $0) } }
        }
        pb.clearContents()
        pb.setString(text, forType: .string)
        tap(Hotkey(keyCode: 9, modifiers: [.command])) // V
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) {
            pb.clearContents()
            let items: [NSPasteboardItem] = saved.map { entries in
                let it = NSPasteboardItem()
                for (t, d) in entries { it.setData(d, forType: t) }
                return it
            }
            if !items.isEmpty { pb.writeObjects(items) }
        }
    }
}
