/// Tracks the 8-bit audio SEQ for loss accounting.
public struct SeqTracker: Sendable {
    private var expected: UInt8?
    public private(set) var received = 0
    public private(set) var lost = 0

    public init() {}

    /// Returns the number of frames missing immediately before this one (0 for duplicates/reordering).
    @discardableResult
    public mutating func observe(_ seq: UInt8) -> Int {
        received += 1
        defer { expected = seq &+ 1 }
        guard let e = expected else { return 0 }
        let gap = Int(seq &- e)
        if gap > 128 { return 0 } // behind the expected value: reordered or duplicate
        lost += gap
        return gap
    }
}
