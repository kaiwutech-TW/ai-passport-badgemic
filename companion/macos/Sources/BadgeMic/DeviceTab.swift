import BadgeMicCore
import SwiftUI

struct DeviceTab: View {
    @EnvironmentObject var model: AppModel

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                GroupBox("連線狀態") {
                    VStack(alignment: .leading, spacing: 6) {
                        row("狀態", model.statusText)
                        row("裝置", model.deviceName ?? "—")
                        row("韌體", model.hello?.fw ?? "—")
                        row("電量", model.battery.map { "\($0)%" } ?? "—")
                        if let h = model.hello, model.protoMismatch {
                            Text("⚠️ 協定版本不符(裝置 proto \(h.proto),App proto \(supportedProtocolVersion))。請更新韌體或 App;收音已停用。")
                                .foregroundStyle(.red)
                        }
                        HStack {
                            Button("重新搜尋") { model.rescan() }
                            Button("中斷連線") { model.disconnectDevice() }.disabled(model.deviceName == nil)
                        }
                    }.frame(maxWidth: .infinity, alignment: .leading).padding(6)
                }

                GroupBox("附近的 Passport") {
                    VStack(alignment: .leading) {
                        if model.discovered.isEmpty { Text("尚未發現裝置(名稱以 Passport- 開頭)").foregroundStyle(.secondary) }
                        ForEach(model.discovered) { d in
                            HStack {
                                Text(d.name); Text("\(d.rssi) dBm").foregroundStyle(.secondary)
                                Spacer()
                                Button("連線") { model.connect(d) }
                            }
                        }
                        Text("首次連線時 macOS 會要求配對,請輸入裝置螢幕上的 6 位數密碼。").font(.caption).foregroundStyle(.secondary)
                    }.frame(maxWidth: .infinity, alignment: .leading).padding(6)
                }

                GroupBox("最近一輪語音") {
                    VStack(alignment: .leading, spacing: 4) {
                        if let t = model.lastTurn {
                            row("輪次", "#\(t.id)\(model.turnActive ? "(進行中)" : "")")
                            row("收到幀數", "\(t.framesReceived)")
                            row("SEQ 缺口", "\(t.lostBySeq)")
                            row("裝置回報", "\(t.deviceFrames.map(String.init) ?? "—") 幀 / 丟 \(t.deviceDropped.map(String.init) ?? "—")")
                            row("結束", t.endReason)
                            if let e = t.encodeText { row("編碼耗時", e) }
                            if let st = t.device?.stackFree { row("堆疊餘", "\(st) B") }
                            if let r = t.device?.readErr, r > 0 { row("讀取錯誤", "\(r)") }
                            if let c = t.device?.clip { row("爆音", "\(c) 幀") }
                        } else { Text("尚無").foregroundStyle(.secondary) }
                    }.frame(maxWidth: .infinity, alignment: .leading).padding(6)
                }

                MicSection()

                DisplaySection()

                GroupBox("記錄") {
                    ScrollView {
                        Text(model.log.suffix(40).joined(separator: "\n"))
                            .font(.system(.caption, design: .monospaced))
                            .frame(maxWidth: .infinity, alignment: .leading).textSelection(.enabled)
                    }.frame(height: 110)
                }
            }.padding()
        }
    }

    private func row(_ k: String, _ v: String) -> some View {
        HStack(alignment: .firstTextBaseline) { Text(k).foregroundStyle(.secondary).frame(width: 80, alignment: .leading); Text(v) }
    }
}

/// Screen settings: mode, timeouts, brightness. Sends only changed fields, validated client-side.
struct DisplaySection: View {
    @EnvironmentObject var model: AppModel

    private func intBinding(_ kp: WritableKeyPath<DisplaySettings, Int?>, fallback: Int) -> Binding<Int> {
        Binding(get: { model.displayDraft[keyPath: kp] ?? fallback },
                set: { model.displayDraft[keyPath: kp] = $0 })
    }

    var body: some View {
        GroupBox("螢幕") {
            VStack(alignment: .leading, spacing: 10) {
                Picker("模式", selection: Binding(get: { model.displayDraft.mode ?? .auto },
                                                  set: { model.displayDraft.mode = $0 })) {
                    Text("自動(閒置調暗/熄屏)").tag(DisplayMode.auto)
                    Text("常亮").tag(DisplayMode.alwaysOn)
                }.pickerStyle(.segmented)

                Stepper("閒置後調暗:\(intBinding(\.dimS, fallback: 30).wrappedValue) 秒",
                        value: intBinding(\.dimS, fallback: 30), in: 5...600, step: 5)
                Stepper("閒置後熄屏:\(offText(intBinding(\.offS, fallback: 120).wrappedValue))",
                        value: intBinding(\.offS, fallback: 120), in: max(5, model.displayDraft.dimS ?? 5)...3600, step: 10)
                Picker("長按關螢幕", selection: Binding(get: { model.displayDraft.offKey ?? OffKey.none },
                                                    set: { model.displayDraft.offKey = $0 })) {
                    Text("不使用").tag(OffKey.none)
                    Text("長按上鍵").tag(OffKey.upLong)
                    Text("長按下鍵").tag(OffKey.downLong)
                }
                HStack {
                    Stepper(sleepText, value: intBinding(\.sleepMin, fallback: 30), in: 0...240, step: 5)
                    Button("永不") { model.displayDraft.sleepMin = 0 }
                }
                HStack { Text("亮度 \(intBinding(\.brightness, fallback: 80).wrappedValue)%").frame(width: 90, alignment: .leading)
                    Slider(value: Binding(get: { Double(intBinding(\.brightness, fallback: 80).wrappedValue) },
                                          set: { model.displayDraft.brightness = Int($0) }), in: 10...100, step: 1) }
                HStack { Text("調暗亮度 \(intBinding(\.dimBrightness, fallback: 20).wrappedValue)%").frame(width: 90, alignment: .leading)
                    Slider(value: Binding(get: { Double(intBinding(\.dimBrightness, fallback: 20).wrappedValue) },
                                          set: { model.displayDraft.dimBrightness = Int($0) }),
                           in: 5...Double(max(5, model.displayDraft.brightness ?? 100)), step: 1) }

                HStack {
                    Button("套用變更") { model.applyDisplayDraft() }
                        .disabled(!model.ready || model.displayDraft.changes(from: model.deviceDisplay).isEmpty)
                    Button("還原為裝置值") { model.displayDraft = model.deviceDisplay; model.displayError = nil }
                    if let e = model.displayError { Text(e).foregroundStyle(.red).font(.caption) }
                }
                Text("只會送出有變更的欄位;下方數值會跟隨裝置回報更新。").font(.caption).foregroundStyle(.secondary)
            }.padding(6)
        }
    }

    private func offText(_ s: Int) -> String { s >= 120 && s % 60 == 0 ? "\(s / 60) 分鐘" : "\(s) 秒" }
    private var sleepText: String {
        let v = intBinding(\.sleepMin, fallback: 30).wrappedValue
        return v == 0 ? "深度睡眠:永不" : "深度睡眠:閒置 \(v) 分鐘後"
    }
}

/// Microphone gain on the badge (ES8311), 0-42 dB in 6 dB steps.
struct MicSection: View {
    @EnvironmentObject var model: AppModel

    var body: some View {
        GroupBox("麥克風") {
            VStack(alignment: .leading, spacing: 8) {
                if let g = model.deviceMicGainDb {
                    Picker("收音增益", selection: Binding(get: { g }, set: { model.setMicGain($0) })) {
                        ForEach(micGainStepsDb, id: \.self) { Text("\($0) dB").tag($0) }
                        // Keep an unexpected device value selectable instead of showing a blank picker.
                        if !micGainStepsDb.contains(g) { Text("\(g) dB").tag(g) }
                    }
                    .disabled(!model.ready)
                    Text("說話太大聲會爆音(錄音畫面音量條變紅);太小聲辨識不到。預設 24 dB。")
                        .font(.caption).foregroundStyle(.secondary)
                } else {
                    Text(model.hello == nil ? "尚未連線" : "此韌體不支援").foregroundStyle(.secondary)
                }
            }.frame(maxWidth: .infinity, alignment: .leading).padding(6)
        }
    }
}
