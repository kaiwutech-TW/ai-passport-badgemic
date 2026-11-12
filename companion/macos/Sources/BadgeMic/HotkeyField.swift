import AppKit
import BadgeMicCore
import SwiftUI

/// Records one key press. (An ObservableObject rather than @State: the SwiftUI macro plugin
/// that expands @State is not shipped with the Command Line Tools.)
final class HotkeyRecorder: ObservableObject {
    @Published var recording = false
    private var monitor: Any?

    func start(_ onCapture: @escaping (Hotkey) -> Void) {
        stop()
        recording = true
        monitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .flagsChanged]) { [weak self] e in
            if e.type == .flagsChanged {
                if e.keyCode == Hotkey.fnKeyCode, e.modifierFlags.contains(.function) {
                    onCapture(.fn); self?.stop(); return nil
                }
                return e
            }
            var m: KeyModifiers = []
            if e.modifierFlags.contains(.command) { m.insert(.command) }
            if e.modifierFlags.contains(.shift) { m.insert(.shift) }
            if e.modifierFlags.contains(.option) { m.insert(.option) }
            if e.modifierFlags.contains(.control) { m.insert(.control) }
            onCapture(Hotkey(keyCode: e.keyCode, modifiers: m))
            self?.stop()
            return nil
        }
    }

    func stop() {
        recording = false
        if let monitor { NSEvent.removeMonitor(monitor) }
        monitor = nil
    }

    deinit { if let monitor { NSEvent.removeMonitor(monitor) } }
}

/// Shows a hotkey and records a new one on request.
struct HotkeyField: View {
    @Binding var hotkey: Hotkey
    @StateObject private var recorder = HotkeyRecorder()

    var body: some View {
        HStack {
            Text(recorder.recording ? "請按下按鍵…" : hotkey.displayString)
                .frame(minWidth: 110, alignment: .leading)
                .padding(.horizontal, 8).padding(.vertical, 3)
                .background(RoundedRectangle(cornerRadius: 5)
                    .strokeBorder(recorder.recording ? Color.accentColor : Color.secondary.opacity(0.4)))
            Button(recorder.recording ? "取消" : "錄製") {
                if recorder.recording { recorder.stop() } else { recorder.start { hotkey = $0 } }
            }
            Button("Fn") { recorder.stop(); hotkey = .fn }
        }
        .onDisappear { recorder.stop() }
    }
}
