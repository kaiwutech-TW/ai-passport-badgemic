import BadgeMicCore
import SwiftUI

struct DictationTab: View {
    @EnvironmentObject var model: AppModel

    private var index: Int? { model.profiles.profiles.firstIndex { $0.id == model.profiles.selectedID } }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                GroupBox("權限") {
                    VStack(alignment: .leading, spacing: 6) {
                        HStack {
                            Image(systemName: model.accessibilityTrusted ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                                .foregroundStyle(model.accessibilityTrusted ? .green : .orange)
                            Text(model.accessibilityTrusted ? "輔助使用:已授權" : "輔助使用:未授權(沒有它就無法送出按鍵)")
                            Spacer()
                            Button("請求授權") { model.requestAccessibility() }
                            Button("開啟系統設定") {
                                NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility")!)
                            }
                        }
                        Text("授權後若仍顯示未授權,請結束並重開 BadgeMic。").font(.caption).foregroundStyle(.secondary)
                    }.padding(6)
                }

                GroupBox("觸發設定檔") {
                    VStack(alignment: .leading, spacing: 10) {
                        Picker("目前使用", selection: $model.profiles.selectedID) {
                            ForEach(model.profiles.profiles) { Text($0.name).tag($0.id) }
                        }
                        if let i = index {
                            ProfileEditor(profile: $model.profiles.profiles[i], devices: model.outputDevices)
                        }
                        HStack {
                            Button("還原預設設定檔") { model.profiles = .defaults }
                            Button("測試音(送到所選裝置)") { model.playTestTone() }
                            Button("測試熱鍵(按住 1 秒)") { model.testHotkey() }
                        }
                    }.padding(6)
                }

                GroupBox("所有輸出裝置") {
                    VStack(alignment: .leading, spacing: 4) {
                        ForEach(model.outputDevices) { d in
                            Text("\(d.name)  ·  \(d.outputChannels) ch\(d.inputChannels > 0 ? "  ·  同時有輸入端" : "")")
                        }
                        if !model.outputDevices.contains(where: { $0.name.localizedCaseInsensitiveContains("blackhole") }) {
                            Text("沒有看到 BlackHole。請安裝 BlackHole 2ch(brew install blackhole-2ch)後重新開啟此頁;辨識工具的麥克風也要選 BlackHole 2ch。")
                                .font(.caption).foregroundStyle(.orange)
                        }
                    }.frame(maxWidth: .infinity, alignment: .leading).padding(6)
                }
            }.padding()
        }
    }
}

struct ProfileEditor: View {
    @Binding var profile: TriggerProfile
    let devices: [AudioDeviceInfo]

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            TextField("名稱", text: $profile.name)
            HStack { Text("熱鍵"); HotkeyField(hotkey: $profile.hotkey) }
            Picker("行為", selection: $profile.behavior) {
                Text("按住(OK 按住期間持續按下)").tag(PttBehavior.hold)
                Text("切換(開始與結束各按一下)").tag(PttBehavior.toggle)
            }
            Stepper("開頭等待 \(profile.leadMs) ms(按鍵後再播音)", value: $profile.leadMs, in: 0...2000, step: 50)
            Stepper("結尾等待 \(profile.tailMs) ms(播完後再放開)", value: $profile.tailMs, in: 0...3000, step: 50)
            Picker("輸出裝置(虛擬麥克風)", selection: Binding(get: { profile.outputDeviceUID ?? "" },
                                                        set: { profile.outputDeviceUID = $0.isEmpty ? nil : $0 })) {
                Text("自動(找 BlackHole)").tag("")
                ForEach(devices) { Text($0.name).tag($0.uid) }
            }
            Toggle("收音期間暫時把系統預設輸入切到虛擬麥克風", isOn: $profile.switchSystemInput)
        }
    }
}
