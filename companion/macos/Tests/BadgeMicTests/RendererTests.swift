import AppKit
import BadgeMicCore
import Foundation
import Testing
import BadgeMicRender

@Suite struct RendererTests {
    @Test func pagesAreExactlyPageSized() {
        var info = BadgeInfo()
        info.name = "王小明"; info.title = "資深工程師"; info.organization = "福樂多股份有限公司"; info.extraLine = "hello@example.com"
        for page in [BadgeRenderer.renderBadge(info), BadgeRenderer.renderQR(info)] {
            #expect(page.image.width == 240 && page.image.height == 320)
            #expect(page.rgba.count == 240 * 320 * 4)
            #expect(BadgeRenderer.rgb565(page).count == RGB565.pageByteCount)
        }
    }

    @Test func chineseTextAndQRDrawNonWhitePixels() {
        var info = BadgeInfo()
        info.name = "王小明"; info.title = "工程師"
        let badge = BadgeRenderer.renderBadge(info)
        // Name band (y 168..220) must contain dark ink.
        var dark = 0
        for y in 168..<220 { for x in 0..<240 where badge.rgba[(y * 240 + x) * 4] < 100 { dark += 1 } }
        #expect(dark > 200)
        let qr = BadgeRenderer.renderQR(info)
        var qrDark = 0
        for y in 70..<270 { for x in 20..<220 where qr.rgba[(y * 240 + x) * 4] < 100 { qrDark += 1 } }
        #expect(qrDark > 3000)
    }

    @Test func dumpPreviewsWhenRequested() throws {
        guard let dir = ProcessInfo.processInfo.environment["BADGEMIC_DUMP_DIR"] else { return }
        var info = BadgeInfo()
        info.name = "王小明"; info.title = "資深韌體工程師"; info.organization = "福樂多股份有限公司"; info.extraLine = "hello@example.com"
        for (i, page) in [BadgeRenderer.renderBadge(info), BadgeRenderer.renderQR(info)].enumerated() {
            let rep = NSBitmapImageRep(cgImage: page.image)
            try rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: "\(dir)/page\(i).png"))
        }
    }
}
