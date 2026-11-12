import Foundation

public enum QRMode: String, Codable, Sendable, CaseIterable { case vcard, custom }

/// What the user typed into the badge editor.
public struct BadgeInfo: Codable, Equatable, Sendable {
    public var name = ""
    public var title = ""
    public var organization = ""
    public var extraLine = ""
    public var photoPath: String?
    public var qrMode: QRMode = .vcard
    public var customQR = ""

    public init() {}

    public var qrPayload: String {
        switch qrMode {
        case .custom: return customQR
        case .vcard: return vCard()
        }
    }

    private static func esc(_ s: String) -> String {
        s.replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: ";", with: "\\;")
            .replacingOccurrences(of: ",", with: "\\,")
            .replacingOccurrences(of: "\n", with: "\\n")
    }

    /// vCard 3.0 built from the fields.
    public func vCard() -> String {
        var lines = ["BEGIN:VCARD", "VERSION:3.0", "N:\(Self.esc(name));;;;", "FN:\(Self.esc(name))"]
        if !organization.isEmpty { lines.append("ORG:\(Self.esc(organization))") }
        if !title.isEmpty { lines.append("TITLE:\(Self.esc(title))") }
        if !extraLine.isEmpty { lines.append("NOTE:\(Self.esc(extraLine))") }
        lines.append("END:VCARD")
        return lines.joined(separator: "\r\n")
    }
}
