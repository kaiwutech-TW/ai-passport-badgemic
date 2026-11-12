import Testing
@testable import BadgeMicCore

private func session(_ behavior: PttBehavior = .hold) -> PttSession {
    PttSession(config: PttConfig(behavior: behavior, leadMs: 200, tailMs: 300))
}

@Suite struct PttSessionTests {
    @Test func normalHoldTurn() {
        var s = session()
        #expect(s.handle(.pttStart(id: 1), at: 0) == [.turnBegan(id: 1), .keyDown])
        #expect(s.phase == .leading)
        #expect(s.nextDeadline == 0.2)
        #expect(s.handle(.audio, at: 0.1).isEmpty)
        #expect(s.handle(.tick, at: 0.2) == [.startAudio, .sendAck(id: 1)])
        #expect(s.phase == .active)
        for i in 1...10 { #expect(s.handle(.audio, at: 0.2 + Double(i) * 0.06).isEmpty) }
        #expect(s.handle(.pttEnd(id: 1), at: 1.0) == [.stopAudio])
        #expect(s.phase == .draining)
        #expect(s.handle(.audioDrained, at: 1.2).isEmpty)
        #expect(s.phase == .tail && s.nextDeadline == 1.5)
        #expect(s.handle(.tick, at: 1.4).isEmpty)
        #expect(s.handle(.tick, at: 1.5) == [.keyUp, .turnEnded(.normal)])
        #expect(s.phase == .idle && s.nextDeadline == nil)
    }

    @Test func endDuringLeadStillPlaysAudioThenReleases() {
        var s = session()
        _ = s.handle(.pttStart(id: 1), at: 0)
        #expect(s.handle(.pttEnd(id: 1), at: 0.05).isEmpty)
        #expect(s.handle(.tick, at: 0.2) == [.startAudio, .sendAck(id: 1), .stopAudio])
        #expect(s.phase == .draining)
        _ = s.handle(.audioDrained, at: 0.3)
        #expect(s.handle(.tick, at: 0.6) == [.keyUp, .turnEnded(.normal)])
    }

    @Test func strayEventsAreIgnored() {
        var s = session()
        #expect(s.handle(.pttEnd(id: 9), at: 0).isEmpty)       // end without start
        #expect(s.handle(.audio, at: 0).isEmpty)
        #expect(s.handle(.audioDrained, at: 0).isEmpty)
        #expect(s.handle(.tick, at: 5).isEmpty)
        _ = s.handle(.pttStart(id: 2), at: 10)
        #expect(s.handle(.pttEnd(id: 3), at: 10.1).isEmpty)    // wrong id
        #expect(s.handle(.pttStart(id: 2), at: 10.1).isEmpty)  // duplicate start
        #expect(s.phase == .leading)
    }

    @Test func zeroFrameTurn() {
        var s = session()
        _ = s.handle(.pttStart(id: 1), at: 0)
        _ = s.handle(.tick, at: 0.2)
        _ = s.handle(.pttEnd(id: 1), at: 0.3)
        _ = s.handle(.audioDrained, at: 0.3)
        #expect(s.handle(.tick, at: 0.6) == [.keyUp, .turnEnded(.normal)])
    }

    @Test func disconnectMidTurnReleasesKey() {
        var s = session()
        _ = s.handle(.pttStart(id: 1), at: 0)
        _ = s.handle(.tick, at: 0.2)
        #expect(s.handle(.disconnect, at: 0.5) == [.cancelAudio, .keyUp, .turnEnded(.disconnect)])
        #expect(s.phase == .idle)
        // During the tail as well.
        _ = s.handle(.pttStart(id: 2), at: 1)
        _ = s.handle(.tick, at: 1.2)
        _ = s.handle(.pttEnd(id: 2), at: 1.3)
        _ = s.handle(.audioDrained, at: 1.3)
        #expect(s.handle(.disconnect, at: 1.4).contains(.keyUp))
    }

    @Test func disconnectWhileIdleDoesNothing() {
        var s = session()
        #expect(s.handle(.disconnect, at: 0).isEmpty)
        #expect(s.handle(.quit, at: 0).isEmpty)
    }

    @Test func quitReleasesKey() {
        var s = session()
        _ = s.handle(.pttStart(id: 1), at: 0)
        #expect(s.handle(.quit, at: 0.1) == [.cancelAudio, .keyUp, .turnEnded(.quit)])
    }

    @Test func watchdogAfterSilence() {
        var s = session()
        _ = s.handle(.pttStart(id: 1), at: 0)
        _ = s.handle(.tick, at: 0.2)
        _ = s.handle(.audio, at: 1.0)
        #expect(s.nextDeadline == 3.0)
        #expect(s.handle(.tick, at: 2.9).isEmpty)
        #expect(s.handle(.tick, at: 3.0) == [.cancelAudio, .keyUp, .turnEnded(.watchdog)])
        #expect(s.phase == .idle)
    }

    @Test func eventsKeepWatchdogAlive() {
        var s = session()
        _ = s.handle(.pttStart(id: 1), at: 0)
        _ = s.handle(.tick, at: 0.2)
        _ = s.handle(.otherEvent, at: 1.9)
        #expect(s.handle(.tick, at: 3.0).isEmpty)
        #expect(s.handle(.tick, at: 3.9) == [.cancelAudio, .keyUp, .turnEnded(.watchdog)])
    }

    @Test func drainTimeout() {
        var s = PttSession(config: PttConfig(leadMs: 0, tailMs: 0, drainTimeout: 5))
        _ = s.handle(.pttStart(id: 1), at: 0)
        _ = s.handle(.pttEnd(id: 1), at: 0.1)
        #expect(s.phase == .draining)
        #expect(s.handle(.tick, at: 5.1) == [.cancelAudio, .keyUp, .turnEnded(.drainTimeout)])
    }

    @Test func toggleTapsAtStartAndEnd() {
        var s = session(.toggle)
        #expect(s.handle(.pttStart(id: 1), at: 0) == [.turnBegan(id: 1), .keyTap])
        _ = s.handle(.tick, at: 0.2)
        _ = s.handle(.pttEnd(id: 1), at: 1.0)
        _ = s.handle(.audioDrained, at: 1.1)
        #expect(s.handle(.tick, at: 1.5) == [.keyTap, .turnEnded(.normal)])
    }

    @Test func toggleAbortStillTapsOff() {
        var s = session(.toggle)
        _ = s.handle(.pttStart(id: 1), at: 0)
        #expect(s.handle(.disconnect, at: 0.5) == [.cancelAudio, .keyTap, .turnEnded(.disconnect)])
    }

    @Test func newTurnSupersedesOld() {
        var s = session()
        _ = s.handle(.pttStart(id: 1), at: 0)
        let out = s.handle(.pttStart(id: 2), at: 0.5)
        #expect(out == [.cancelAudio, .keyUp, .turnEnded(.superseded), .turnBegan(id: 2), .keyDown])
    }

    @Test func configSnapshotPerTurn() {
        var s = session()
        _ = s.handle(.pttStart(id: 1), at: 0)
        s.config.behavior = .toggle   // changed mid-turn: must not affect the running turn
        #expect(s.handle(.disconnect, at: 0.1).contains(.keyUp))
    }
}
