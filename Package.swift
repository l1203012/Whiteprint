// swift-tools-version:5.8
// Keep the target graph in sync with Scripts/targets.sh, which builds the
// same modules with plain swiftc when only the Command Line Tools are installed.
import PackageDescription

let package = Package(
    name: "Whiteprint",
    platforms: [.macOS(.v13)],
    products: [
        .executable(name: "WhiteprintApp", targets: ["WhiteprintApp"]),
        .executable(name: "whiteprint-mcp", targets: ["whiteprint-mcp"]),
        .library(name: "WhiteprintCore", targets: ["WhiteprintCore"]),
    ],
    targets: [
        // Platform-independent: no AppKit, so it can be reused for a Windows build later.
        .target(name: "WhiteprintCore"),
        .target(name: "WhiteprintExtract"),
        .target(name: "WhiteprintRender", dependencies: ["WhiteprintCore"]),
        .target(name: "WhiteprintBridge", dependencies: ["WhiteprintCore"]),
        .target(name: "WhiteprintStudy", dependencies: ["WhiteprintCore", "WhiteprintExtract"]),
        .target(name: "WhiteprintEditor", dependencies: ["WhiteprintCore", "WhiteprintRender"]),
        .executableTarget(name: "WhiteprintApp", dependencies: [
            "WhiteprintCore", "WhiteprintExtract", "WhiteprintRender",
            "WhiteprintBridge", "WhiteprintStudy", "WhiteprintEditor",
        ]),
        .executableTarget(name: "whiteprint-mcp", dependencies: ["WhiteprintCore", "WhiteprintBridge"]),

        .testTarget(name: "WhiteprintCoreTests", dependencies: ["WhiteprintCore"]),
        .testTarget(name: "WhiteprintBridgeTests", dependencies: ["WhiteprintBridge", "WhiteprintCore"]),
        .testTarget(name: "WhiteprintStudyTests", dependencies: ["WhiteprintStudy"]),
        .testTarget(name: "WhiteprintExtractTests", dependencies: ["WhiteprintExtract"]),
    ]
)
