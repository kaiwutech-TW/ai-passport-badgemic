import BadgeMicCore
import SwiftUI

private enum ActionKind: String, CaseIterable, Identifiable {
    case none, shortcut, returnKey, escape, text, shortcutsApp, openApp
    var id: String { rawValue }
    var label: String {
        switch self {
        case .none: return "無動作"
        case .shortcut: return "鍵盤快捷鍵"
        case .returnKey: return "Return"
        case .escape: return "Escape"
        case .text: return "輸入文字片段"
        case .shortcutsApp: return "執行「捷徑」"
        case .openApp: return "開啟 App"
        }
    }
    init(_ a: ButtonAction) {
        switch a {
        case .none: self = .none
        case .keyShortcut: self = .shortcut
        case .returnKey: self = .returnKey
        case .escape: self = .escape
        case .typeText: self = .text
        case .runShortcut: self = .shortcutsApp
        case .openApp: self = .openApp
        }
    }
    var empty: ButtonAction {
        switch self {
        case .none: return .none
        case .shortcut: return .keyShortcut(Hotkey(keyCode: 126, modifiers: [.control]))
        case .returnKey: return .returnKey
        case .escape: return .escape
        case .text: return .typeText("")
        case .shortcutsApp: return .runShortcut("")
        case .openApp: return .openApp("")
        }
    }
}

struct ButtonsTab: View {
    @EnvironmentObject var model: AppModel

    private func title(_ s: ButtonSlot) -> String {
        "\(s.key == .up ? "UP" : "DOWN") · \(s.gesture == .click ? "短按" : "長按")"
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                ForEach(ButtonSlot.allCases, id: \.storageKey) { slot in
                    GroupBox(title(slot)) {
                        SlotEditor(action: Binding(get: { model.mappings[slot] }, set: { model.mappings[slot] = $0 }))
                            .padding(6)
                    }
                }
                Text("OK 鍵固定用於收音/切換名牌頁,不能對應動作。螢幕關閉時第一次按 UP/DOWN 只會喚醒螢幕。")
                    .font(.caption).foregroundStyle(.secondary)
            }.padding()
        }
    }
}

private struct SlotEditor: View {
    @Binding var action: ButtonAction

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Picker("動作", selection: Binding(get: { ActionKind(action) }, set: { action = $0.empty })) {
                ForEach(ActionKind.allCases) { Text($0.label).tag($0) }
            }
            switch action {
            case .keyShortcut(let hk):
                HStack { Text("快捷鍵"); HotkeyField(hotkey: Binding(get: { hk }, set: { action = .keyShortcut($0) })) }
            case .typeText(let t):
                TextField("要輸入的文字(透過剪貼簿 + ⌘V,結束後還原剪貼簿)", text: Binding(get: { t }, set: { action = .typeText($0) }))
            case .runShortcut(let n):
                TextField("捷徑名稱(shortcuts run)", text: Binding(get: { n }, set: { action = .runShortcut($0) }))
            case .openApp(let a):
                TextField("App 名稱或 .app 路徑", text: Binding(get: { a }, set: { action = .openApp($0) }))
            default: EmptyView()
            }
        }
    }
}
