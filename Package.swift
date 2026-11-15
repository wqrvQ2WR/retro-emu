// swift-tools-version:5.9
import PackageDescription
let package = Package(
  name: "RetroEmu",
  platforms: [.macOS(.v13)],
  targets: [
    .target(name: "CRhost", path: "host", exclude: ["test.c", "libretro.h"], sources: ["rhost.c"], publicHeadersPath: "include",
            cSettings: [.headerSearchPath(".")]),
    .executableTarget(name: "RetroEmu", dependencies: ["CRhost"], path: "mac/Sources/RetroEmu"),
  ]
)
