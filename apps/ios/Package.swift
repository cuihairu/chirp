// swift-tools-version:6.0
// chirp iOS — SwiftPM protocol package. Language mode stays .v5 on purpose:
// the connection is a lock-serialized port (dart's single event loop → one
// recursive lock) and strict concurrency would demand Sendable surgery
// without adding safety the lock doesn't already provide. The committed
// gencode in Sources/ChirpProtos was produced by protoc-gen-swift 1.38.1
// (matching the pinned SwiftProtobuf below).
import PackageDescription

let package = Package(
    name: "chirp-ios",
    // WsTransportDarwin 的 URLSessionWebSocketTask 是 iOS 13/macOS 10.15+。
    // 不声明时 SwiftPM 按 iOS 12 下限编译包——Xcode 里 app 工程（部署目标
    // 17）引本地包照样按包自身下限编，可用性检查即挂（首跑 CI 实测）。
    // Linux 门禁不受 platforms 影响。
    platforms: [
        .iOS(.v13),
        .macOS(.v10_15),
    ],
    products: [
        .library(name: "ChirpProtocol", targets: ["ChirpProtocol"]),
        // UI-free app logic (state machine drafts, device identity, host
        // config): lives in the package so `swift test` covers it on both
        // Linux (the local gate) and macOS (the CI leg) — the SwiftUI shell
        // in ChirpCompanion/ stays thin over it.
        .library(name: "ChirpAppCore", targets: ["ChirpAppCore"]),
    ],
    dependencies: [
        .package(url: "https://github.com/apple/swift-protobuf.git", exact: "1.38.1"),
    ],
    targets: [
        .target(
            name: "ChirpProtos",
            dependencies: [.product(name: "SwiftProtobuf", package: "swift-protobuf")],
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .target(
            name: "ChirpProtocol",
            dependencies: ["ChirpProtos"],
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .target(
            name: "ChirpAppCore",
            dependencies: ["ChirpProtocol"],
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .testTarget(
            name: "ChirpProtocolTests",
            dependencies: ["ChirpProtocol", "ChirpProtos"],
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .testTarget(
            name: "ChirpAppCoreTests",
            dependencies: ["ChirpAppCore"],
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
    ]
)
