import AppKit
import BadgeMicCore
import BadgeMicRender
import BadgeMicOpus
import Combine
import CoreAudio
import Foundation
import SwiftUI

let appVersion = "0.1.0"

struct TurnStats: Equatable {
    var id: Int
    var framesReceived = 0
    var lostBySeq = 0
    var deviceFrames: Int?
    var deviceDropped: Int?
    var endReason: String = "進行中"
}

enum UploadError: Error, LocalizedError {
    case timeout, device(String), notReady, busy
    var errorDescription: String? {
        switch self {
        case .timeout: return "裝置逾時未回應"
        case .device(let m): return "裝置回報失敗:\(m)"
        case .notReady: return "裝置尚未就緒"
        case .busy: return "正在收音或上傳中"
        }
    }
}

@MainActor
final class AppModel: ObservableObject {
    // MARK: Persisted settings
    @Published var profiles: ProfileSettings { didSet { persist("profiles.v1", profiles) } }
    @Published var mappings: ButtonMappings { didSet { persist("mappings.v1", mappings) } }
    @Published var badge: BadgeInfo { didSet { persist("badge.v1", badge) } }

    // MARK: Device state
    @Published var link: LinkState = .idle
    @Published var discovered: [DiscoveredDevice] = []
    @Published var deviceName: String?
    @Published var hello: HelloInfo?
    @Published var ready = false
    @Published var protoMismatch = false
    @Published var battery: Int?
    @Published var deviceDisplay = DisplaySettings.defaults
    @Published var displayDraft = DisplaySettings.defaults
    @Published var displayError: String?
    @Published var lastTurn: TurnStats?
    @Published var turnActive = false
    @Published var log: [String] = []

    // MARK: Environment
    @Published var accessibilityTrusted = false
    @Published var outputDevices: [AudioDeviceInfo] = []

    // MARK: Upload
    @Published var uploadProgress: Double?
    @Published var uploadMessage: String?

    private let transport = BLETransport()
    private let injector = KeyInjector()
    private let audio = AudioOutput()
    private lazy var runner = ButtonActionRunner(injector: injector)
    private var decoder: OpusDecoder?
    private var ptt = PttSession()
    private var tickWork: DispatchWorkItem?
    private var reassemblerSeq = SeqTracker()
    private var turnProfile: TriggerProfile?
    private var savedInputDevice: AudioDeviceID?
    private var currentStats: TurnStats?
    private var signalSources: [DispatchSourceSignal] = []
    private var pollTimer: Timer?

    /// Requests the device hello with get_hello until it arrives (1 s, then every 1.5 s, 8 tries).
    private func scheduleHelloRequest(attempt: Int) {
        guard attempt <= 8 else { addLog("裝置沒有回 hello,請重新連線"); return }
        DispatchQueue.main.asyncAfter(deadline: .now() + (attempt == 1 ? 1.0 : 1.5)) { [weak self] in
            guard let self, self.hello == nil, self.link == .subscribed else { return }
            self.addLog("未收到 hello,向裝置索取(第 \(attempt) 次)")
            self.send(.getHello())
            self.scheduleHelloRequest(attempt: attempt + 1)
        }
    }

    private var uploadEvents: [DeviceEvent] = []
    private var uploadWaiter: CheckedContinuation<DeviceEvent, Error>?
    private var uploading = false

    init() {
        let d = UserDefaults.standard
        profiles = SettingsCodec.decodeProfiles(d.data(forKey: "profiles.v1"))
        mappings = SettingsCodec.decodeMappings(d.data(forKey: "mappings.v1"))
        badge = SettingsCodec.decodeBadge(d.data(forKey: "badge.v1"))
        decoder = try? OpusDecoder()
        refreshEnvironment()
        wire()
        installSafetyNets()
        let last = d.string(forKey: "lastPeripheral").flatMap(UUID.init(uuidString:))
        transport.start(lastDevice: last)
        pollTimer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.refreshEnvironment() }
        }
    }

    // MARK: - Helpers

    private func persist<T: Encodable>(_ key: String, _ value: T) {
        if let data = try? SettingsCodec.encode(value) { UserDefaults.standard.set(data, forKey: key) }
    }

    func addLog(_ s: String) {
        log.append(s)
        if log.count > 200 { log.removeFirst(log.count - 200) }
    }

    func refreshEnvironment() {
        accessibilityTrusted = KeyInjector.isTrusted(prompt: false)
        let devs = AudioDevices.outputDevices()
        if devs != outputDevices { outputDevices = devs }
    }

    func requestAccessibility() { accessibilityTrusted = KeyInjector.isTrusted(prompt: true) }

    var selectedProfile: TriggerProfile { profiles.selected }

    var statusText: String {
        switch link {
        case .unavailable(let m): return m
        case .idle: return "未連線"
        case .scanning: return "搜尋中…"
        case .connecting: return "連線中…"
        case .discovering: return "配對/設定中…(若出現配對視窗請輸入裝置上的 6 位數)"
        case .subscribed: return ready ? (protoMismatch ? "協定版本不符" : "已連線") : "等待裝置 hello…"
        }
    }

    var menuIcon: String {
        if turnActive { return "mic.fill" }
        return ready && !protoMismatch ? "person.crop.rectangle.fill" : "person.crop.rectangle"
    }

    /// Output device for a profile: explicit UID, else a BlackHole-looking device.
    private func resolveOutput(_ p: TriggerProfile) -> AudioDeviceInfo? {
        if let uid = p.outputDeviceUID, let d = AudioDevices.device(uid: uid) { return d }
        return p.outputDeviceUID == nil ? AudioDevices.guessVirtualMic() : nil
    }

    // MARK: - Wiring

    private func wire() {
        transport.onLog = { [weak self] in self?.addLog($0) }
        transport.onState = { [weak self] in self?.link = $0 }
        transport.onDiscovered = { [weak self] in self?.discovered = $0 }
        transport.onRememberDevice = { UserDefaults.standard.set($0.uuidString, forKey: "lastPeripheral") }
        transport.onConnected = { [weak self] name in
            guard let self else { return }
            self.deviceName = name
            // The device sends its hello as soon as notifications are enabled, so that hello can
            // arrive before this callback. Never clear readiness here (disconnect already does);
            // only ask again if it has not arrived yet.
            if self.hello == nil {
                self.addLog("已訂閱通知,等待 hello")
                self.scheduleHelloRequest(attempt: 1)
            }
        }
        transport.onDisconnected = { [weak self] in
            guard let self else { return }
            self.ready = false
            self.hello = nil
            self.battery = nil
            self.failUpload(UploadError.notReady)
            self.pttInput(.disconnect)
        }
        transport.onFrame = { [weak self] in self?.handle(frame: $0) }
        audio.onDrained = { [weak self] in self?.pttInput(.audioDrained) }
    }

    /// Release the key no matter how we exit.
    private func installSafetyNets() {
        NotificationCenter.default.addObserver(forName: NSApplication.willTerminateNotification, object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.emergencyRelease(.quit) }
        }
        for sig in [SIGTERM, SIGINT, SIGHUP] {
            signal(sig, SIG_IGN)
            let src = DispatchSource.makeSignalSource(signal: sig, queue: .main)
            src.setEventHandler { [weak self] in
                MainActor.assumeIsolated { self?.emergencyRelease(.quit) }
                NSApplication.shared.terminate(nil)
            }
            src.resume()
            signalSources.append(src)
        }
    }

    private func emergencyRelease(_ input: PttInput) {
        pttInput(input)
        injector.releaseIfHeld()
        restoreSystemInput()
        audio.shutdown()
    }

    // MARK: - Device frames

    private func handle(frame: Frame) {
        switch frame.type {
        case .audioOpus: handleAudio(frame.payload)
        case .event:
            if let ev = DeviceEvent.parse(frame.payload) { handle(event: ev) } else { addLog("忽略無法解析的事件") }
        default: break
        }
    }

    private func handleAudio(_ payload: [UInt8]) {
        guard payload.count >= 2, ptt.phase != .idle, let decoder else { return }
        let lost = reassemblerSeq.observe(payload[0])
        currentStats?.framesReceived += 1
        if lost > 0 {
            currentStats?.lostBySeq += lost
            for _ in 0..<min(lost, 3) { if let pcm = try? decoder.conceal() { audio.enqueue(pcm) } }
        }
        if let pcm = try? decoder.decode(Array(payload.dropFirst())) { audio.enqueue(pcm) }
        lastTurn = currentStats
        pttInput(.audio)
    }

    private func handle(event: DeviceEvent) {
        if uploading, case .putReady = event { deliverUpload(event) }
        if uploading, case .putDone = event { deliverUpload(event) }

        switch event {
        case .hello(let h):
            hello = h
            battery = h.battery >= 0 ? h.battery : nil
            if let d = h.display {
                deviceDisplay = DisplaySettings.defaults.merged(with: d)
                displayDraft = deviceDisplay
            }
            protoMismatch = h.proto != supportedProtocolVersion
            if protoMismatch { addLog("協定版本不符:裝置 \(h.proto)、App \(supportedProtocolVersion)。已停用收音。") }
            ready = true
            send(.hello(version: appVersion))
        case .pttStart(let id):
            guard ready, !protoMismatch else { addLog("忽略 ptt_start(未就緒或版本不符)"); return }
            beginTurn(id: id)
        case .pttEnd(let id, let frames, let dropped, let reason):
            currentStats?.deviceFrames = frames
            currentStats?.deviceDropped = dropped
            currentStats?.endReason = reason ?? "正常"
            lastTurn = currentStats
            pttInput(.pttEnd(id: id))
        case .key(let key, let gesture):
            pttInput(.otherEvent)
            runner.run(mappings.action(for: key, gesture), log: addLog)
        case .display(let d):
            deviceDisplay = deviceDisplay.merged(with: d)
            displayDraft = deviceDisplay
        case .battery(let b):
            battery = b >= 0 ? b : nil
            pttInput(.otherEvent)
        case .error(let cmd, let err):
            addLog("裝置錯誤 \(cmd ?? "?"): \(err ?? "?")")
        case .putReady, .putDone, .unknown:
            pttInput(.otherEvent)
        }
    }

    // MARK: - Push to talk driver

    private func beginTurn(id: Int) {
        let profile = selectedProfile
        turnProfile = profile
        ptt.config = profile.pttConfig
        reassemblerSeq = SeqTracker()
        decoder?.reset()
        currentStats = TurnStats(id: id)
        lastTurn = currentStats
        pttInput(.pttStart(id: id))
    }

    private func now() -> TimeInterval { ProcessInfo.processInfo.systemUptime }

    private func pttInput(_ input: PttInput) {
        let actions = ptt.handle(input, at: now())
        for a in actions { execute(a) }
        scheduleTick()
    }

    private func scheduleTick() {
        tickWork?.cancel()
        tickWork = nil
        guard let deadline = ptt.nextDeadline else { return }
        let work = DispatchWorkItem { [weak self] in
            MainActor.assumeIsolated { self?.pttInput(.tick) }
        }
        tickWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + max(0, deadline - now()) + 0.001, execute: work)
    }

    private func execute(_ action: PttAction) {
        let profile = turnProfile ?? selectedProfile
        switch action {
        case .turnBegan:
            turnActive = true
            prepareAudio(for: profile)
        case .keyDown:
            if !accessibilityTrusted { addLog("警告:尚未授權輔助使用,按鍵不會生效") }
            injector.press(profile.hotkey)
        case .keyUp:
            injector.release()
        case .keyTap:
            injector.tap(profile.hotkey)
        case .startAudio:
            audio.play()
        case .stopAudio:
            audio.finishInput()
        case .cancelAudio:
            audio.cancel()
        case .sendAck(let id):
            send(.pttAck(id: id))
        case .turnEnded(let reason):
            injector.releaseIfHeld()
            restoreSystemInput()
            audio.shutdown()
            turnActive = false
            if reason != .normal {
                currentStats?.endReason = "中止(\(reason))"
                addLog("本輪語音中止:\(reason)")
            }
            lastTurn = currentStats
        }
    }

    private func prepareAudio(for profile: TriggerProfile) {
        guard let dev = resolveOutput(profile) else {
            addLog("找不到輸出裝置:請在「聽寫」分頁選擇虛擬麥克風(例如 BlackHole 2ch)")
            return
        }
        do { try audio.prepare(deviceID: dev.id) } catch { addLog("音訊輸出失敗:\(error.localizedDescription)") }
        if profile.switchSystemInput, dev.inputChannels > 0 {
            savedInputDevice = AudioDevices.defaultInput()
            if !AudioDevices.setDefaultInput(dev.id) { addLog("切換系統輸入裝置失敗") }
        }
    }

    private func restoreSystemInput() {
        if let saved = savedInputDevice {
            AudioDevices.setDefaultInput(saved)
            savedInputDevice = nil
        }
    }

    // MARK: - Sending

    func send(_ cmd: ControlCommand) {
        guard let bytes = try? cmd.frame() else { return }
        Task { [weak self] in
            do { try await self?.transport.write(bytes) } catch { self?.addLog("送出 \(cmd.cmd) 失敗:\(error.localizedDescription)") }
        }
    }

    func connect(_ d: DiscoveredDevice) { transport.connect(to: d.id) }
    func disconnectDevice() { transport.disconnect() }
    func rescan() { transport.rescan() }

    // MARK: - Display

    /// Validates the draft and sends only the fields that differ from what the device reported.
    func applyDisplayDraft() {
        let draft = displayDraft
        if let err = draft.validationError() { displayError = err; return }
        displayError = nil
        let diff = draft.changes(from: deviceDisplay)
        guard !diff.isEmpty else { return }
        send(.setDisplay(diff))
    }

    func setDisplayMode(_ mode: DisplayMode) {
        displayDraft.mode = mode
        applyDisplayDraft()
    }

    // MARK: - Test helpers

    func playTestTone() {
        let profile = selectedProfile
        guard let dev = resolveOutput(profile) else { addLog("請先選擇輸出裝置"); return }
        do {
            try audio.prepare(deviceID: dev.id)
            let tone = (0..<16_000).map { Float(0.3 * sin(2 * Double.pi * 440 * Double($0) / 16_000)) }
            audio.enqueue(tone)
            audio.play()
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { [weak self] in
                if self?.ptt.phase == .idle { self?.audio.shutdown() }
            }
        } catch { addLog("測試音失敗:\(error.localizedDescription)") }
    }

    func testHotkey() {
        let hk = selectedProfile.hotkey
        guard accessibilityTrusted else { addLog("尚未授權輔助使用"); return }
        injector.press(hk)
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) { [weak self] in self?.injector.release() }
    }

    // MARK: - Upload

    private func deliverUpload(_ e: DeviceEvent) {
        if let w = uploadWaiter { uploadWaiter = nil; w.resume(returning: e) } else { uploadEvents.append(e) }
    }

    private func failUpload(_ error: Error) {
        if let w = uploadWaiter { uploadWaiter = nil; w.resume(throwing: error) }
    }

    private func nextUploadEvent(timeout: TimeInterval) async throws -> DeviceEvent {
        if !uploadEvents.isEmpty { return uploadEvents.removeFirst() }
        let timer = DispatchWorkItem { [weak self] in
            MainActor.assumeIsolated { self?.failUpload(UploadError.timeout) }
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + timeout, execute: timer)
        defer { timer.cancel() }
        return try await withCheckedThrowingContinuation { uploadWaiter = $0 }
    }

    func page(_ index: Int) -> BadgeRenderer.Page {
        index == 0 ? BadgeRenderer.renderBadge(badge) : BadgeRenderer.renderQR(badge)
    }

    func upload(pages: [Int]) async {
        guard ready, !protoMismatch else { uploadMessage = UploadError.notReady.localizedDescription; return }
        guard !uploading, ptt.phase == .idle else { uploadMessage = UploadError.busy.localizedDescription; return }
        uploading = true
        defer { uploading = false; uploadProgress = nil }
        do {
            for (n, idx) in pages.enumerated() {
                uploadMessage = "上傳第 \(idx + 1) 頁(\(n + 1)/\(pages.count))…"
                try await upload(page: idx, base: Double(n) / Double(pages.count), span: 1 / Double(pages.count))
            }
            uploadMessage = "上傳完成"
        } catch {
            uploadMessage = "上傳失敗:\(error.localizedDescription)"
        }
    }

    private func upload(page idx: Int, base: Double, span: Double) async throws {
        let data = BadgeRenderer.rgb565(page(idx))
        uploadEvents.removeAll()
        uploadProgress = base
        try await transport.write(try ControlCommand.putBegin(page: idx, size: data.count, crc32: CRC32.checksum(data)).frame())
        switch try await nextUploadEvent(timeout: 10) {
        case .putReady: break
        case .putDone(_, _, let err): throw UploadError.device(err ?? "拒絕上傳")
        default: throw UploadError.timeout
        }
        let chunks = UploadPlanner.plan(totalSize: data.count)
        for (i, c) in chunks.enumerated() {
            let payload = UploadPlanner.putDataPayload(offset: c.offset, data: data[c.offset..<(c.offset + c.length)])
            try await transport.write(try FrameCodec.encode(type: .putData, payload: payload))
            if i % 8 == 0 { uploadProgress = base + span * Double(i) / Double(chunks.count) }
            if !ready { throw UploadError.notReady }
        }
        try await transport.write(try ControlCommand.putEnd(page: idx).frame())
        switch try await nextUploadEvent(timeout: 20) {
        case .putDone(_, true, _): uploadProgress = base + span
        case .putDone(_, false, let err): throw UploadError.device(err ?? "未知錯誤")
        default: throw UploadError.timeout
        }
    }

    func clearPage(_ idx: Int) {
        send(.clearPage(idx))
        uploadMessage = "已要求清除第 \(idx + 1) 頁"
    }
}
