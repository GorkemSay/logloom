// swift-tools-version: 5.10

import Foundation
import PackageDescription

let packageDir = URL(fileURLWithPath: #filePath).deletingLastPathComponent().path
let repoRoot = URL(fileURLWithPath: packageDir).deletingLastPathComponent().path
let libDir = "\(repoRoot)/build/release/core"

let package = Package(
    name: "Logloom",
    platforms: [
        .macOS(.v14),
    ],
    products: [
        .library(name: "LogloomCore", targets: ["LogloomCore"]),
        .executable(name: "LogloomApp", targets: ["LogloomApp"]),
    ],
    targets: [
        .target(
            name: "CLogloom",
            path: "Sources/CLogloom",
            publicHeadersPath: "include"
        ),
        .target(
            name: "LogloomCore",
            dependencies: ["CLogloom"],
            path: "Sources/LogloomCore",
            linkerSettings: [
                .unsafeFlags(["-L\(libDir)"]),
                .linkedLibrary("logloom"),
                .linkedLibrary("c++"),
            ]
        ),
        .executableTarget(
            name: "LogloomApp",
            dependencies: ["LogloomCore"],
            path: "Sources/LogloomApp"
        ),
        .testTarget(
            name: "LogloomCoreTests",
            dependencies: ["LogloomCore"],
            path: "Tests/LogloomCoreTests"
        ),
    ]
)
