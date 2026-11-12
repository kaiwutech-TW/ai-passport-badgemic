import BadgeMicCore
import SwiftUI

struct MenuBarView: View {
    @EnvironmentObject var model: AppModel
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        Text(model.statusText)
        if let b = model.battery { Text("電量 \(b)%") }
        if model.protoMismatch { Text("協定版本不符,已停用收音") }
        Divider()
        Picker("觸發設定檔", selection: $model.profiles.selectedID) {
            ForEach(model.profiles.profiles) { Text($0.name).tag($0.id) }
        }
        Picker("螢幕", selection: Binding(
            get: { model.displayDraft.mode ?? .auto },
            set: { model.setDisplayMode($0) })) {
            Text("自動").tag(DisplayMode.auto)
            Text("常亮").tag(DisplayMode.alwaysOn)
        }
        Divider()
        Button("開啟設定…") {
            openWindow(id: "main")
            NSApp.activate(ignoringOtherApps: true)
        }
        Button("結束 BadgeMic") { NSApplication.shared.terminate(nil) }
            .keyboardShortcut("q")
    }
}
