import AppKit
import BadgeMicCore
import CoreGraphics
import CoreImage
import CoreImage.CIFilterBuiltins
import CoreText

/// Renders badge pages at exactly 240 x 320 with CoreGraphics / CoreText.
/// Layout coordinates are top-left based (y grows downward) and converted internally.
public enum BadgeRenderer {
    public static let width = RGB565.pageWidth
    public static let height = RGB565.pageHeight

    public struct Page {
        public let image: CGImage
        public let rgba: [UInt8]   // 240 * 320 * 4, RGBX, top row first
    }

    private static let accent = CGColor(red: 0.07, green: 0.30, blue: 0.55, alpha: 1)
    private static let ink = CGColor(red: 0.08, green: 0.09, blue: 0.11, alpha: 1)
    private static let muted = CGColor(red: 0.35, green: 0.37, blue: 0.41, alpha: 1)
    private static let paper = CGColor(red: 1, green: 1, blue: 1, alpha: 1)

    private static func makeContext() -> CGContext {
        let ctx = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
                            space: CGColorSpace(name: CGColorSpace.sRGB)!,
                            bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)!
        ctx.setAllowsAntialiasing(true)
        ctx.setShouldSmoothFonts(true)
        ctx.interpolationQuality = .high
        ctx.setFillColor(paper)
        ctx.fill(CGRect(x: 0, y: 0, width: width, height: height))
        return ctx
    }

    private static func finish(_ ctx: CGContext) -> Page {
        let count = width * height * 4
        let rgba = [UInt8](UnsafeBufferPointer(start: ctx.data!.assumingMemoryBound(to: UInt8.self), count: count))
        return Page(image: ctx.makeImage()!, rgba: rgba)
    }

    private static func flip(_ r: CGRect) -> CGRect {
        CGRect(x: r.minX, y: CGFloat(height) - r.maxY, width: r.width, height: r.height)
    }

    // MARK: Text

    private static func attributed(_ s: String, size: CGFloat, bold: Bool, color: CGColor, align: CTTextAlignment) -> NSAttributedString {
        let font = CTFontCreateWithName((bold ? "PingFangTC-Semibold" : "PingFangTC-Regular") as CFString, size, nil)
        var a = align
        var lineBreak = CTLineBreakMode.byWordWrapping
        let style: CTParagraphStyle = withUnsafePointer(to: &a) { ap in
            withUnsafePointer(to: &lineBreak) { lp in
                let settings = [
                    CTParagraphStyleSetting(spec: .alignment, valueSize: MemoryLayout<CTTextAlignment>.size, value: ap),
                    CTParagraphStyleSetting(spec: .lineBreakMode, valueSize: MemoryLayout<CTLineBreakMode>.size, value: lp),
                ]
                return CTParagraphStyleCreate(settings, settings.count)
            }
        }
        return NSAttributedString(string: s, attributes: [
            NSAttributedString.Key(kCTFontAttributeName as String): font,
            NSAttributedString.Key(kCTForegroundColorAttributeName as String): color,
            NSAttributedString.Key(kCTParagraphStyleAttributeName as String): style,
        ])
    }

    private static func measure(_ s: NSAttributedString, width: CGFloat) -> CGSize {
        let fs = CTFramesetterCreateWithAttributedString(s)
        return CTFramesetterSuggestFrameSizeWithConstraints(fs, CFRangeMake(0, 0), nil,
                                                            CGSize(width: width, height: .greatestFiniteMagnitude), nil)
    }

    /// Draws `text` into `rect` (top-left based), shrinking from `maxSize` to `minSize` so it fits.
    /// Prefers a single line; falls back to wrapping at the minimum size, clipped to the rect.
    @discardableResult
    private static func drawFit(_ ctx: CGContext, _ text: String, rect: CGRect, maxSize: CGFloat, minSize: CGFloat,
                                bold: Bool, color: CGColor, align: CTTextAlignment = .center, singleLine: Bool = true) -> CGFloat {
        guard !text.isEmpty else { return 0 }
        var size = maxSize
        var chosen = minSize
        while size >= minSize {
            let s = attributed(text, size: size, bold: bold, color: color, align: align)
            let h = measure(s, width: rect.width).height
            let limit = singleLine ? size * 1.45 : rect.height
            if h <= limit && h <= rect.height { chosen = size; break }
            size -= 1
            chosen = minSize
        }
        let s = attributed(text, size: chosen, bold: bold, color: color, align: align)
        let h = min(measure(s, width: rect.width).height, rect.height)
        // Vertically centre inside the rect.
        let drawRect = CGRect(x: rect.minX, y: rect.minY + (rect.height - h) / 2, width: rect.width, height: h)
        let fs = CTFramesetterCreateWithAttributedString(s)
        let path = CGPath(rect: flip(drawRect), transform: nil)
        let frame = CTFramesetterCreateFrame(fs, CFRangeMake(0, 0), path, nil)
        ctx.saveGState()
        ctx.clip(to: flip(rect))
        CTFrameDraw(frame, ctx)
        ctx.restoreGState()
        return h
    }

    // MARK: Pages

    public static func renderBadge(_ info: BadgeInfo) -> Page {
        let ctx = makeContext()
        let W = CGFloat(width), H = CGFloat(height)

        ctx.setFillColor(accent)
        ctx.fill(flip(CGRect(x: 0, y: 0, width: W, height: 10)))
        ctx.fill(flip(CGRect(x: 0, y: H - 6, width: W, height: 6)))

        // Portrait: circular photo (or initial) centred at the top.
        let circle = CGRect(x: W / 2 - 62, y: 30, width: 124, height: 124)
        ctx.saveGState()
        ctx.addEllipse(in: flip(circle))
        ctx.clip()
        if let path = info.photoPath, let img = NSImage(contentsOfFile: path),
           let cg = img.cgImage(forProposedRect: nil, context: nil, hints: nil) {
            let iw = CGFloat(cg.width), ih = CGFloat(cg.height)
            let scale = max(circle.width / iw, circle.height / ih)
            let dw = iw * scale, dh = ih * scale
            ctx.draw(cg, in: flip(CGRect(x: circle.midX - dw / 2, y: circle.midY - dh / 2, width: dw, height: dh)))
        } else {
            ctx.setFillColor(CGColor(red: 0.88, green: 0.93, blue: 0.98, alpha: 1))
            ctx.fill(flip(circle))
        }
        ctx.restoreGState()
        if info.photoPath == nil || NSImage(contentsOfFile: info.photoPath ?? "") == nil {
            let initial = info.name.trimmingCharacters(in: .whitespaces).first.map { String($0) } ?? "?"
            drawFit(ctx, initial, rect: circle, maxSize: 64, minSize: 30, bold: true, color: accent)
        }
        ctx.setStrokeColor(accent)
        ctx.setLineWidth(3)
        ctx.strokeEllipse(in: flip(circle.insetBy(dx: 1.5, dy: 1.5)))

        let name = info.name.isEmpty ? "姓名" : info.name
        drawFit(ctx, name, rect: CGRect(x: 10, y: 168, width: W - 20, height: 52), maxSize: 36, minSize: 20,
                bold: true, color: info.name.isEmpty ? muted : ink)
        // Accent rule under the name.
        ctx.setFillColor(accent)
        ctx.fill(flip(CGRect(x: W / 2 - 24, y: 226, width: 48, height: 3)))
        drawFit(ctx, info.title, rect: CGRect(x: 12, y: 236, width: W - 24, height: 26), maxSize: 18, minSize: 12,
                bold: true, color: accent)
        drawFit(ctx, info.organization, rect: CGRect(x: 12, y: 264, width: W - 24, height: 24), maxSize: 16, minSize: 11,
                bold: false, color: ink)
        drawFit(ctx, info.extraLine, rect: CGRect(x: 12, y: 288, width: W - 24, height: 20), maxSize: 13, minSize: 10,
                bold: false, color: muted)
        return finish(ctx)
    }

    public static func renderQR(_ info: BadgeInfo) -> Page {
        let ctx = makeContext()
        let W = CGFloat(width), H = CGFloat(height)
        ctx.setFillColor(accent)
        ctx.fill(flip(CGRect(x: 0, y: 0, width: W, height: 10)))
        ctx.fill(flip(CGRect(x: 0, y: H - 6, width: W, height: 6)))

        drawFit(ctx, info.name.isEmpty ? "姓名" : info.name, rect: CGRect(x: 10, y: 20, width: W - 20, height: 40),
                maxSize: 26, minSize: 16, bold: true, color: ink)

        let area = CGRect(x: 20, y: 70, width: 200, height: 200)
        // At least 2 px per module so the 240 px screen stays scannable; otherwise show the error text.
        if let qr = qrImage(info.qrPayload), qr.width * 2 <= Int(area.width) {
            let modules = qr.width
            let px = max(1, Int(area.width) / modules)           // integer module size keeps edges crisp
            let side = CGFloat(modules * px)
            let target = CGRect(x: area.midX - side / 2, y: area.midY - side / 2, width: side, height: side)
            ctx.saveGState()
            ctx.interpolationQuality = .none
            ctx.draw(qr, in: flip(target))
            ctx.restoreGState()
        } else {
            let msg = info.qrPayload.isEmpty ? "尚無 QR 內容" : "內容過長,無法產生 QR"
            drawFit(ctx, msg, rect: area, maxSize: 16, minSize: 12, bold: false, color: muted)
        }
        drawFit(ctx, info.qrMode == .vcard ? "掃描加入聯絡人" : "掃描 QR Code", rect: CGRect(x: 12, y: 280, width: W - 24, height: 26),
                maxSize: 15, minSize: 11, bold: false, color: muted)
        return finish(ctx)
    }

    /// QR modules as a 1-pixel-per-module CGImage (black on white, no quiet zone; the white page provides it).
    private static func qrImage(_ text: String) -> CGImage? {
        guard !text.isEmpty else { return nil }
        let f = CIFilter.qrCodeGenerator()
        f.message = Data(text.utf8)
        f.correctionLevel = "M"
        guard let out = f.outputImage else { return nil }
        let ci = CIContext(options: [.useSoftwareRenderer: true])
        return ci.createCGImage(out, from: out.extent)
    }

    public static func rgb565(_ page: Page) -> [UInt8] {
        RGB565.convert(rgba: page.rgba, width: width, height: height)
    }
}
