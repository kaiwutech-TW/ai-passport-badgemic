import Foundation

public enum RGB565 {
    public static let pageWidth = 240
    public static let pageHeight = 320
    public static let pageByteCount = pageWidth * pageHeight * 2 // 153 600

    /// Converts tightly-packed 8-bit RGBA/RGBX pixels (4 bytes each, R first) to RGB565 little-endian.
    public static func convert(rgba: [UInt8], width: Int, height: Int) -> [UInt8] {
        precondition(rgba.count >= width * height * 4, "pixel buffer too small")
        var out = [UInt8](repeating: 0, count: width * height * 2)
        for p in 0..<(width * height) {
            let r = UInt16(rgba[p * 4]), g = UInt16(rgba[p * 4 + 1]), b = UInt16(rgba[p * 4 + 2])
            let v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            out[p * 2] = UInt8(v & 0xFF)
            out[p * 2 + 1] = UInt8(v >> 8)
        }
        return out
    }
}
