import Foundation

public enum PttBehavior: String, Codable, Sendable, CaseIterable {
    case hold    // key down for the whole turn
    case toggle  // tap at the start and again at the end
}

public struct PttConfig: Equatable, Sendable {
    public var behavior: PttBehavior
    public var leadMs: Int
    public var tailMs: Int
    /// Abort a turn when nothing arrived for this long.
    public var watchdog: TimeInterval
    /// Upper bound on waiting for queued audio to drain after ptt_end.
    public var drainTimeout: TimeInterval

    public init(behavior: PttBehavior = .hold, leadMs: Int = 200, tailMs: Int = 300,
                watchdog: TimeInterval = 2.0, drainTimeout: TimeInterval = 10.0) {
        self.behavior = behavior; self.leadMs = leadMs; self.tailMs = tailMs
        self.watchdog = watchdog; self.drainTimeout = drainTimeout
    }
}

public enum PttInput: Equatable, Sendable {
    case pttStart(id: Int)
    case pttEnd(id: Int)
    case audio            // any audio frame
    case otherEvent       // any other device event that proves the link is alive
    case audioDrained     // the audio engine finished playing everything queued
    case disconnect
    case quit
    case tick             // time passed; call at `nextDeadline`
}

public enum PttEndReason: Equatable, Sendable { case normal, watchdog, disconnect, quit, superseded, drainTimeout }

public enum PttAction: Equatable, Sendable {
    case turnBegan(id: Int)
    case keyDown          // hold: press and keep pressed
    case keyUp            // hold: release
    case keyTap           // toggle: press+release (start and end of the turn)
    case startAudio       // begin playing the queued audio
    case stopAudio        // no more input for this turn: play what is queued, then report audioDrained
    case cancelAudio      // discard queued audio immediately
    case sendAck(id: Int)
    case turnEnded(PttEndReason)
}

public enum PttPhase: Equatable, Sendable { case idle, leading, active, draining, tail }

/// Pure push-to-talk state machine: inputs (with timestamps) in, actions out.
/// The driver calls `handle(.tick, at:)` when `nextDeadline` is reached.
public struct PttSession: Sendable {
    public private(set) var phase: PttPhase = .idle
    public private(set) var turnId: Int?
    public var config: PttConfig

    private var deadline: TimeInterval = 0
    private var lastActivity: TimeInterval = 0
    private var pendingEnd = false
    private var activeConfig: PttConfig

    public init(config: PttConfig = PttConfig()) {
        self.config = config
        self.activeConfig = config
    }

    public var nextDeadline: TimeInterval? {
        switch phase {
        case .idle: return nil
        case .leading: return min(deadline, lastActivity + activeConfig.watchdog)
        case .active: return lastActivity + activeConfig.watchdog
        case .draining, .tail: return deadline
        }
    }

    private var release: PttAction { activeConfig.behavior == .hold ? .keyUp : .keyTap }

    public mutating func handle(_ input: PttInput, at now: TimeInterval) -> [PttAction] {
        var out: [PttAction] = []
        switch input {
        case .pttStart(let id):
            if phase != .idle {
                if id == turnId { return [] } // duplicate start
                out += abort(.superseded)
            }
            activeConfig = config
            turnId = id
            lastActivity = now
            pendingEnd = false
            phase = .leading
            deadline = now + Double(activeConfig.leadMs) / 1000
            out.append(.turnBegan(id: id))
            out.append(activeConfig.behavior == .hold ? .keyDown : .keyTap)
        case .audio, .otherEvent:
            if phase == .leading || phase == .active { lastActivity = now }
        case .pttEnd(let id):
            guard id == turnId else { return [] } // stray end
            switch phase {
            case .leading:
                lastActivity = now
                pendingEnd = true
            case .active:
                out.append(.stopAudio)
                phase = .draining
                deadline = now + activeConfig.drainTimeout
            default: return []
            }
        case .audioDrained:
            if phase == .draining {
                phase = .tail
                deadline = now + Double(activeConfig.tailMs) / 1000
            }
        case .disconnect:
            if phase != .idle { out += abort(.disconnect) }
        case .quit:
            if phase != .idle { out += abort(.quit) }
        case .tick:
            break
        }
        out += advance(now)
        return out
    }

    private mutating func abort(_ reason: PttEndReason) -> [PttAction] {
        phase = .idle
        turnId = nil
        pendingEnd = false
        return [.cancelAudio, release, .turnEnded(reason)]
    }

    private mutating func advance(_ now: TimeInterval) -> [PttAction] {
        var out: [PttAction] = []
        // At most a couple of transitions can chain (leading -> draining -> tail).
        for _ in 0..<4 {
            switch phase {
            case .idle:
                return out
            case .leading, .active:
                if now - lastActivity >= activeConfig.watchdog { return out + abort(.watchdog) }
                guard phase == .leading, now >= deadline else { return out }
                out.append(.startAudio)
                if let id = turnId { out.append(.sendAck(id: id)) }
                if pendingEnd {
                    pendingEnd = false
                    out.append(.stopAudio)
                    phase = .draining
                    deadline = now + activeConfig.drainTimeout
                } else {
                    phase = .active
                }
            case .draining:
                guard now >= deadline else { return out }
                return out + abort(.drainTimeout)
            case .tail:
                guard now >= deadline else { return out }
                phase = .idle
                turnId = nil
                return out + [release, .turnEnded(.normal)]
            }
        }
        return out
    }
}
