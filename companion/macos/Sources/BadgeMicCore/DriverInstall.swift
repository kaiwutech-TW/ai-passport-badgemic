import Foundation

/// Builds the privileged install command for the bundled virtual microphone driver.
/// Pure so the quoting can be unit-tested; the app runs it through osascript.
public enum DriverInstall {
    public static let driverName = "BadgeMicAudio.driver"
    public static let halDir = "/Library/Audio/Plug-Ins/HAL"
    public static var installedPath: String { "\(halDir)/\(driverName)" }

    /// Single-quotes a path for /bin/sh.
    static func shQuote(_ s: String) -> String { "'" + s.replacingOccurrences(of: "'", with: "'\\''") + "'" }

    /// Escapes text for an AppleScript string literal.
    static func asQuote(_ s: String) -> String {
        "\"" + s.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"") + "\""
    }

    /// Shell command run as root: replace the driver, clear quarantine, restart coreaudiod.
    public static func shellCommand(source: String) -> String {
        let dest = shQuote(installedPath)
        return "mkdir -p \(shQuote(halDir)) && rm -rf \(dest) && cp -R \(shQuote(source)) \(dest)"
            + " && chown -R root:wheel \(dest) && (xattr -dr com.apple.quarantine \(dest) || true)"
            + " && (killall coreaudiod || launchctl kickstart -kp system/com.apple.audio.coreaudiod)"
    }

    /// AppleScript that asks for an administrator password and runs `shellCommand`.
    public static func appleScript(source: String, prompt: String) -> String {
        "do shell script \(asQuote(shellCommand(source: source))) with prompt \(asQuote(prompt)) with administrator privileges"
    }
}
