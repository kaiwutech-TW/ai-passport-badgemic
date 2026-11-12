import AppKit
import BadgeMicCore

/// Executes the action mapped to an UP/DOWN gesture.
@MainActor
final class ButtonActionRunner {
    let injector: KeyInjector
    init(injector: KeyInjector) { self.injector = injector }

    func run(_ action: ButtonAction, log: (String) -> Void) {
        switch action {
        case .none:
            break
        case .keyShortcut(let hk):
            injector.tap(hk)
        case .returnKey:
            injector.tap(.returnKey)
        case .escape:
            injector.tap(.escape)
        case .typeText(let text):
            injector.typeText(text)
        case .runShortcut(let name):
            launch("/usr/bin/shortcuts", ["run", name], log: log)
        case .openApp(let app):
            // Accepts an app name ("Safari") or a path to a .app bundle.
            if app.hasPrefix("/") {
                launch("/usr/bin/open", [app], log: log)
            } else {
                launch("/usr/bin/open", ["-a", app], log: log)
            }
        }
    }

    private func launch(_ path: String, _ args: [String], log: (String) -> Void) {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: path)
        p.arguments = args
        p.standardOutput = FileHandle.nullDevice
        p.standardError = FileHandle.nullDevice
        do { try p.run() } catch { log("執行失敗:\(path) \(args.joined(separator: " ")) (\(error.localizedDescription))") }
    }
}
