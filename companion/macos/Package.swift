// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "BadgeMic",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "BadgeMic", targets: ["BadgeMic"]),
        .library(name: "BadgeMicCore", targets: ["BadgeMicCore"]),
    ],
    targets: [
        // libopus from Homebrew, found through pkg-config.
        .systemLibrary(name: "COpus", pkgConfig: "opus", providers: [.brew(["opus"])]),
        // Pure logic: framing, protocol, CRC, RGB565, upload planning, PTT state machine, models.
        .target(name: "BadgeMicCore"),
        // Thin Swift wrapper around libopus (decoder for the app, encoder for tests).
        .target(name: "BadgeMicOpus", dependencies: ["COpus"]),
        // CoreGraphics / CoreText / CoreImage page renderer (240x320).
        .target(name: "BadgeMicRender", dependencies: ["BadgeMicCore"]),
        // The macOS app: BLE, audio output, key injection, rendering, SwiftUI.
        .executableTarget(name: "BadgeMic", dependencies: ["BadgeMicCore", "BadgeMicOpus", "BadgeMicRender"]),
        .testTarget(name: "BadgeMicTests", dependencies: ["BadgeMicCore", "BadgeMicOpus", "BadgeMicRender"]),
    ]
)
