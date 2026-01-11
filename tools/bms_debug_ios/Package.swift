// swift-tools-version: 5.7
import PackageDescription
import AppleProductTypes

let package = Package(
    name: "BmsDebugApp",
    platforms: [
        .iOS(.v15)
    ],
    products: [
        .iOSApplication(
            name: "BmsDebug",
            targets: ["BmsDebugApp"],
            bundleIdentifier: "com.example.bmsdebug",
            teamIdentifier: "ABCDE12345",
            displayVersion: "0.1",
            bundleVersion: "1",
            iconAssetName: "AppIcon",
            accentColorAssetName: "AccentColor",
            supportedDeviceFamilies: [
                .pad,
                .phone
            ],
            supportedInterfaceOrientations: [
                .portrait,
                .landscapeRight,
                .landscapeLeft
            ]
        )
    ],
    targets: [
        .executableTarget(
            name: "BmsDebugApp",
            path: "Sources/BmsDebugApp"
        )
    ]
)
