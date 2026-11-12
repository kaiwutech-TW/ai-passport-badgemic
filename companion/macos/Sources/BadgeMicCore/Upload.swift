import Foundation

public struct UploadChunk: Equatable, Sendable {
    public let offset: Int
    public let length: Int
}

public enum UploadPlanner {
    public static let maxChunk = 224

    public static func plan(totalSize: Int, maxChunk: Int = UploadPlanner.maxChunk) -> [UploadChunk] {
        precondition(maxChunk > 0 && maxChunk <= UploadPlanner.maxChunk)
        var chunks: [UploadChunk] = []
        var offset = 0
        while offset < totalSize {
            let len = min(maxChunk, totalSize - offset)
            chunks.append(UploadChunk(offset: offset, length: len))
            offset += len
        }
        return chunks
    }

    /// Builds the PUT_DATA frame payload: 4-byte big-endian offset followed by the data.
    public static func putDataPayload(offset: Int, data: ArraySlice<UInt8>) -> [UInt8] {
        let o = UInt32(offset)
        return [UInt8(o >> 24), UInt8((o >> 16) & 0xFF), UInt8((o >> 8) & 0xFF), UInt8(o & 0xFF)] + data
    }
}
