import SwiftUI

struct RootView: View {
    var body: some View {
        TabView {
            DeviceTab().tabItem { Label("裝置", systemImage: "dot.radiowaves.left.and.right") }
            DictationTab().tabItem { Label("聽寫", systemImage: "mic") }
            ButtonsTab().tabItem { Label("按鍵", systemImage: "square.grid.2x2") }
            BadgeTab().tabItem { Label("名牌", systemImage: "person.crop.rectangle") }
        }
        .padding()
        .frame(minWidth: 700, minHeight: 560)
    }
}
