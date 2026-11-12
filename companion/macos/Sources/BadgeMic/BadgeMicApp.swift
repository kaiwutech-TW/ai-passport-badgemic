import SwiftUI

@main
struct BadgeMicApp: App {
    @StateObject private var model = AppModel()

    var body: some Scene {
        MenuBarExtra("BadgeMic", systemImage: model.menuIcon) {
            MenuBarView().environmentObject(model)
        }
        .menuBarExtraStyle(.menu)

        Window("BadgeMic", id: "main") {
            RootView().environmentObject(model)
        }
        .defaultSize(width: 760, height: 640)
    }
}
