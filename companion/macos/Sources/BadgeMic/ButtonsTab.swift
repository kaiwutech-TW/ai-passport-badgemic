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

    /// The long press of this key is consumed by the device to turn the screen off.
    private func isOffKey(_ s: ButtonSlot) -> Bool {
        guard s.gesture == .long else { return false }
        switch model.deviceDisplay.offKey {
        case .upLong: return s.key == .up
        case .downLong: return s.key == .down
        default: return false
        }
    }

    private static let screenOffTag = "screenOff"

    /// Long-press picker: the usual Mac actions plus "turn the badge screen off" (device setting off_key).
    private func longPicker(_ slot: ButtonSlot, reserved: Bool) -> some View {
        Picker("動作", selection: Binding<String>(
            get: { reserved ? Self.screenOffTag : ActionKind(model.mappings[slot]).rawValue },
            set: { v in
                if v == Self.screenOffTag {
                    model.setOffKey(slot.key == .up ? .upLong : .downLong)
                } else {
                    if reserved { model.setOffKey(.none) }
                    model.mappings[slot] = (ActionKind(rawValue: v) ?? .none).empty
                }
            })) {
            ForEach(ActionKind.allCases) { Text($0.label).tag($0.rawValue) }
            Divider()
            Text("關閉名牌螢幕").tag(Self.screenOffTag)
        }
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                ForEach(ButtonSlot.allCases, id: \.storageKey) { slot in
                    let reserved = isOffKey(slot)
                    let binding = Binding(get: { model.mappings[slot] }, set: { model.mappings[slot] = $0 })
                    GroupBox(title(slot)) {
                        VStack(alignment: .leading, spacing: 8) {
                            if slot.gesture == .long {
                                longPicker(slot, reserved: reserved)
                                if reserved {
                                    Text("在名牌上長按此鍵即關閉螢幕(名牌自己處理,不連 Mac 也有效);再按任一鍵喚醒。")
                                        .font(.caption).foregroundStyle(.secondary)
                                } else {
                                    SlotEditor(action: binding, showPicker: false)
                                }
                            } else {
                                SlotEditor(action: binding, showPicker: true)
                            }
                        }.padding(6)
                    }
                }
                Text("OK 鍵固定用於收音/切換名牌頁,不能對應動作。螢幕關閉時第一次按 UP/DOWN 只會喚醒螢幕。「關閉名牌螢幕」需名牌連線中才能設定。")
                    .font(.caption).foregroundStyle(.secondary)
            }.padding()
        }
    }
}

private struct SlotEditor: View {
    @Binding var action: ButtonAction
    var showPicker = true

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            if showPicker {
                Picker("動作", selection: Binding(get: { ActionKind(action) }, set: { action = $0.empty })) {
                    ForEach(ActionKind.allCases) { Text($0.label).tag($0) }
                }
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
