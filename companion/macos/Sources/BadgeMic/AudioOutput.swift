import AVFoundation
import AudioToolbox

enum AudioOutputError: Error, LocalizedError {
    case noDevice, deviceNotFound(String), engine(String)
    var errorDescription: String? {
        switch self {
        case .noDevice: return "尚未選擇輸出裝置(虛擬麥克風)"
        case .deviceNotFound(let uid): return "找不到輸出裝置 \(uid)"
        case .engine(let m): return "音訊引擎錯誤:\(m)"
        }
    }
}

/// Plays 16 kHz mono float PCM into a chosen output device (a virtual microphone),
/// resampling to the device format with AVAudioConverter.
final class AudioOutput {
    /// Called on the main queue once everything queued has been played after `finishInput()`.
    var onDrained: (() -> Void)?

    private var engine: AVAudioEngine?
    private var player: AVAudioPlayerNode?
    private var converter: AVAudioConverter?
    private var outFormat: AVAudioFormat?
    private let inFormat = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: 16_000, channels: 1, interleaved: false)!
    private var pending = 0
    private var inputFinished = false
    private var generation = 0
    private(set) var isPrepared = false

    /// Builds the engine for `deviceID` and starts it. Call before the hotkey goes down so the lead time covers startup.
    func prepare(deviceID: AudioDeviceID) throws {
        shutdown()
        generation += 1
        pending = 0
        inputFinished = false

        let engine = AVAudioEngine()
        let player = AVAudioPlayerNode()
        guard let au = engine.outputNode.audioUnit else { throw AudioOutputError.engine("no output unit") }
        var dev = deviceID
        let status = AudioUnitSetProperty(au, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
                                          &dev, UInt32(MemoryLayout<AudioDeviceID>.size))
        if status != noErr { throw AudioOutputError.engine("set device failed (\(status))") }

        let hw = engine.outputNode.outputFormat(forBus: 0)
        let rate = hw.sampleRate > 0 ? hw.sampleRate : 48_000
        let channels = AVAudioChannelCount(max(1, min(2, hw.channelCount)))
        guard let fmt = AVAudioFormat(standardFormatWithSampleRate: rate, channels: channels),
              let conv = AVAudioConverter(from: inFormat, to: fmt) else {
            throw AudioOutputError.engine("cannot build converter")
        }
        engine.attach(player)
        engine.connect(player, to: engine.mainMixerNode, format: fmt)
        engine.prepare()
        do { try engine.start() } catch { throw AudioOutputError.engine(error.localizedDescription) }
        self.engine = engine; self.player = player; self.converter = conv; self.outFormat = fmt
        isPrepared = true
    }

    /// Queues samples; they are not heard until `play()`.
    func enqueue(_ samples: [Float]) {
        guard isPrepared, let player, let converter, let outFormat, !samples.isEmpty else { return }
        guard let src = AVAudioPCMBuffer(pcmFormat: inFormat, frameCapacity: AVAudioFrameCount(samples.count)) else { return }
        src.frameLength = AVAudioFrameCount(samples.count)
        samples.withUnsafeBufferPointer { src.floatChannelData![0].update(from: $0.baseAddress!, count: samples.count) }

        let ratio = outFormat.sampleRate / inFormat.sampleRate
        let cap = AVAudioFrameCount(Double(samples.count) * ratio) + 64
        guard let dst = AVAudioPCMBuffer(pcmFormat: outFormat, frameCapacity: cap) else { return }
        var supplied = false
        var error: NSError?
        let status = converter.convert(to: dst, error: &error) { _, outStatus in
            if supplied { outStatus.pointee = .noDataNow; return nil }
            supplied = true
            outStatus.pointee = .haveData
            return src
        }
        guard status != .error, dst.frameLength > 0 else { return }

        pending += 1
        let gen = generation
        player.scheduleBuffer(dst, completionCallbackType: .dataPlayedBack) { [weak self] _ in
            DispatchQueue.main.async {
                guard let self, self.generation == gen else { return }
                self.pending -= 1
                self.checkDrained()
            }
        }
    }

    func play() {
        guard isPrepared, let player, !player.isPlaying else { return }
        player.play()
    }

    /// No more input is coming: report `onDrained` when the queue is empty.
    func finishInput() {
        inputFinished = true
        let gen = generation
        DispatchQueue.main.async { [weak self] in
            guard let self, self.generation == gen else { return }
            self.checkDrained()
        }
    }

    private func checkDrained() {
        if inputFinished && pending <= 0 {
            inputFinished = false
            onDrained?()
        }
    }

    /// Drops everything queued (used when a turn is aborted).
    func cancel() {
        generation += 1
        pending = 0
        inputFinished = false
        player?.stop()
    }

    func shutdown() {
        generation += 1
        player?.stop()
        engine?.stop()
        engine = nil; player = nil; converter = nil; outFormat = nil
        isPrepared = false
        pending = 0
        inputFinished = false
    }
}
