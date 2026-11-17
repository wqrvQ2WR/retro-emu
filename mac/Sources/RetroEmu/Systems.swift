import AppKit
import SwiftUI

/// 지원하는 기종. 확장자로 기종과 코어를 고른다.
enum GameSystem: String, CaseIterable, Codable, Identifiable {
  case nes, gb, gbc, gba, snes, md, ps1, n64, nds, gc, psp, ps2

  var id: String { rawValue }

  var title: String {
    switch self {
    case .nes: return "패미컴"
    case .gb: return "게임보이"
    case .gbc: return "게임보이 컬러"
    case .gba: return "게임보이 어드밴스"
    case .snes: return "슈퍼패미컴"
    case .md: return "메가드라이브"
    case .ps1: return "플레이스테이션"
    case .n64: return "닌텐도 64"
    case .nds: return "닌텐도 DS"
    case .gc: return "게임큐브"
    case .psp: return "PSP"
    case .ps2: return "플레이스테이션 2"
    }
  }

  var core: String {
    switch self {
    case .nes: return "fceumm_libretro"
    case .gb, .gbc, .gba: return "mgba_libretro"
    case .snes: return "snes9x_libretro"
    case .md: return "genesis_plus_gx_libretro"
    case .ps1: return "pcsx_rearmed_libretro"
    case .n64: return "mupen64plus_next_libretro"
    case .nds: return "melonds_libretro"
    case .gc: return "dolphin_libretro"
    case .psp: return "ppsspp_libretro"
    case .ps2: return "play_libretro"
    }
  }

  var extensions: [String] {
    switch self {
    case .nes: return ["nes", "fds", "unf"]
    case .gb: return ["gb"]
    case .gbc: return ["gbc"]
    case .gba: return ["gba"]
    case .snes: return ["sfc", "smc", "swc", "fig"]
    case .md: return ["md", "gen", "smd"]
    case .ps1: return ["cue", "chd", "pbp", "m3u"]
    case .n64: return ["n64", "z64", "v64"]
    case .nds: return ["nds"]
    case .gc: return ["gcm", "rvz", "gcz", "ciso", "wbfs"]
    case .psp: return ["cso"]
    case .ps2: return []   // .iso 는 안을 보고 고른다(sniff)
    }
  }

  /// 카드에 넣는 게임기 사진(퍼블릭 도메인, assets/consoles/CREDITS.md). 앱 Resources/consoles 에 들어간다.
  var photo: NSImage? {
    GameSystem.photoCache[self] ?? {
      let img = Bundle.main.resourcePath.flatMap { NSImage(contentsOfFile: $0 + "/consoles/\(rawValue).png") }
      GameSystem.photoCache[self] = img
      return img
    }()
  }
  private static var photoCache: [GameSystem: NSImage?] = [:]
  /// 세로로 긴 휴대용 기기
  var tallPhoto: Bool { self == .gb || self == .gbc }

  var gradient: [Color] {
    switch self {
    case .nes: return [Color(red: 1.0, green: 0.30, blue: 0.40), Color(red: 1.0, green: 0.62, blue: 0.20)]
    case .gb: return [Color(red: 0.45, green: 0.85, blue: 0.25), Color(red: 0.10, green: 0.75, blue: 0.55)]
    case .gbc: return [Color(red: 0.62, green: 0.36, blue: 1.0), Color(red: 0.24, green: 0.66, blue: 1.0)]
    case .gba: return [Color(red: 0.40, green: 0.30, blue: 1.0), Color(red: 1.0, green: 0.30, blue: 0.62)]
    case .snes: return [Color(red: 0.62, green: 0.36, blue: 1.0), Color(red: 1.0, green: 0.30, blue: 0.62)]
    case .md: return [Color(red: 0.12, green: 0.55, blue: 1.0), Color(red: 0.20, green: 0.85, blue: 0.90)]
    case .ps1: return [Color(red: 1.0, green: 0.72, blue: 0.10), Color(red: 1.0, green: 0.38, blue: 0.20)]
    case .n64: return [Color(red: 0.15, green: 0.75, blue: 0.35), Color(red: 1.0, green: 0.80, blue: 0.15)]
    case .nds: return [Color(red: 0.25, green: 0.70, blue: 1.0), Color(red: 0.62, green: 0.40, blue: 1.0)]
    case .gc: return [Color(red: 0.45, green: 0.30, blue: 0.95), Color(red: 0.25, green: 0.80, blue: 0.85)]
    case .psp: return [Color(red: 0.20, green: 0.30, blue: 0.90), Color(red: 0.95, green: 0.30, blue: 0.70)]
    case .ps2: return [Color(red: 0.15, green: 0.40, blue: 1.0), Color(red: 0.55, green: 0.25, blue: 0.95)]
    }
  }

  /// 확장자로 기종 찾기. .bin 은 같은 폴더의 .cue 가 가리키면 PS1 데이터(게임 아님), 아니면 메가드라이브.
  static func from(path: String) -> GameSystem? {
    let ext = (path as NSString).pathExtension.lowercased()
    if ext == "iso" { return sniffISO(path) }
    if ext == "pbp" { return sniffPBP(path) }
    if ext == "bin" {
      let dir = (path as NSString).deletingLastPathComponent
      return cueReferenced(dir: dir).contains((path as NSString).lastPathComponent.lowercased()) ? nil : .md
    }
    return allCases.first { $0.extensions.contains(ext) }
  }

  /// 폴더 안 .cue 파일들이 FILE "x.bin" 으로 가리키는 파일 이름(소문자)
  static func cueReferenced(dir: String) -> Set<String> {
    var out = Set<String>()
    let names = (try? FileManager.default.contentsOfDirectory(atPath: dir)) ?? []
    for n in names where n.lowercased().hasSuffix(".cue") {
      guard let t = try? String(contentsOfFile: dir + "/" + n, encoding: .isoLatin1) else { continue }
      for line in t.split(whereSeparator: \.isNewline) {
        let l = line.trimmingCharacters(in: .whitespaces)
        guard l.uppercased().hasPrefix("FILE"), let a = l.firstIndex(of: "\""), let b = l.lastIndex(of: "\""), a < b else { continue }
        out.insert(String(l[l.index(after: a)..<b]).lowercased())
      }
    }
    return out
  }

  static var allExtensions: [String] { allCases.flatMap { $0.extensions } + ["bin", "zip", "iso"] }

  private static func bytes(_ path: String, at off: UInt64, count: Int) -> [UInt8] {
    guard let f = FileHandle(forReadingAtPath: path) else { return [] }
    defer { try? f.close() }
    try? f.seek(toOffset: off)
    return [UInt8](f.readData(ofLength: count))
  }

  /// .iso 는 게임큐브·위(디스크 머리의 표식), PSP(ISO9660 시스템 이름 "PSP GAME"), 그 밖은 PS2 로 본다.
  /// 규칙은 안드로이드 Systems.kt, 윈도우 main.c 와 같다.
  static func sniffISO(_ path: String) -> GameSystem {
    let h = bytes(path, at: 0, count: 0x20)
    if h.count == 0x20 {
      if h[0x1C...0x1F] == [0xC2, 0x33, 0x9F, 0x3D] || h[0x18...0x1B] == [0x5D, 0x1C, 0x9E, 0xA3] { return .gc }
    }
    let v = bytes(path, at: 0x8000, count: 40)
    if v.count == 40, String(bytes: v[1..<6], encoding: .ascii) == "CD001",
       String(bytes: v[8..<40], encoding: .ascii)?.hasPrefix("PSP GAME") == true { return .psp }
    return .ps2
  }

  /// .pbp 는 PSP 게임(EBOOT)과 PS1 게임(PS1 EBOOT) 둘 다 쓴다. DATA.PSAR 머리가 PSISOIMG/PSTITLEIMG 면 PS1.
  static func sniffPBP(_ path: String) -> GameSystem {
    let h = bytes(path, at: 0, count: 0x28)
    guard h.count == 0x28, h[0...3] == [0, 0x50, 0x42, 0x50] else { return .ps1 }
    let off = UInt64(h[0x24]) | UInt64(h[0x25]) << 8 | UInt64(h[0x26]) << 16 | UInt64(h[0x27]) << 24
    let tag = String(bytes: bytes(path, at: off, count: 8), encoding: .ascii) ?? ""
    return tag == "PSISOIMG" || tag == "PSTITLEI" ? .ps1 : .psp
  }

  /// 아날로그 스틱이 주된 조작인 기종(키보드 방향키를 왼쪽 스틱으로 보낸다)
  var stickFirst: Bool { self == .n64 || self == .gc }
  /// 아날로그 스틱이 있는 기종
  var hasAnalog: Bool { self == .n64 || self == .gc || self == .psp || self == .ps2 || self == .ps1 }
  /// 아래 화면을 손가락·마우스로 누르는 기종
  var hasTouch: Bool { self == .nds }
  /// 처음 켤 때 시스템 폴더에 풀어 둘 자료(앱에 sysassets/<이름> 으로 들어 있다)
  var sysAssets: String? {
    switch self { case .psp: return "PPSSPP"; case .gc: return "dolphin-emu"; default: return nil }
  }
}
