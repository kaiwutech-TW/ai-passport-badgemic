import Foundation

public enum QRMode: String, Codable, Sendable, CaseIterable { case vcard, custom }

/// What the user typed into the badge editor.
public struct BadgeInfo: Codable, Equatable, Sendable {
    public var name = ""
    public var title = ""
    public var organization = ""
    public var extraLine = ""
    public var phone = ""
    public var email = ""
    public var website = ""
    public var photoPath: String?
    public var qrMode: QRMode = .vcard
    public var customQR = ""

    public init() {}

    enum CodingKeys: String, CodingKey {
        case name, title, organization, extraLine, phone, email, website, photoPath, qrMode, customQR
    }

    /// Tolerant decoding: JSON saved by older versions lacks the newer keys.
    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        name = try c.decodeIfPresent(String.self, forKey: .name) ?? ""
        title = try c.decodeIfPresent(String.self, forKey: .title) ?? ""
        organization = try c.decodeIfPresent(String.self, forKey: .organization) ?? ""
        extraLine = try c.decodeIfPresent(String.self, forKey: .extraLine) ?? ""
        phone = try c.decodeIfPresent(String.self, forKey: .phone) ?? ""
        email = try c.decodeIfPresent(String.self, forKey: .email) ?? ""
        website = try c.decodeIfPresent(String.self, forKey: .website) ?? ""
        photoPath = try c.decodeIfPresent(String.self, forKey: .photoPath)
        qrMode = try c.decodeIfPresent(QRMode.self, forKey: .qrMode) ?? .vcard
        customQR = try c.decodeIfPresent(String.self, forKey: .customQR) ?? ""
    }

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
        if !phone.isEmpty { lines.append("TEL;TYPE=CELL:\(Self.esc(phone))") }
        if !email.isEmpty { lines.append("EMAIL;TYPE=INTERNET:\(Self.esc(email))") }
        if !website.isEmpty { lines.append("URL:\(Self.esc(website))") }
        if !extraLine.isEmpty { lines.append("NOTE:\(Self.esc(extraLine))") }
        lines.append("END:VCARD")
        return lines.joined(separator: "\r\n")
    }
}
