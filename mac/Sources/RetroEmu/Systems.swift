import SwiftUI

/// 지원하는 기종. 확장자로 기종과 코어를 고른다.
enum GameSystem: String, CaseIterable, Codable, Identifiable {
  case nes, gb, gbc, gba, snes, md, ps1

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
    }
  }

  var core: String {
    switch self {
    case .nes: return "fceumm_libretro"
    case .gb, .gbc, .gba: return "mgba_libretro"
    case .snes: return "snes9x_libretro"
    case .md: return "genesis_plus_gx_libretro"
    case .ps1: return "pcsx_rearmed_libretro"
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
    }
  }

  var gradient: [Color] {
    switch self {
    case .nes: return [Color(red: 1.0, green: 0.30, blue: 0.40), Color(red: 1.0, green: 0.62, blue: 0.20)]
    case .gb: return [Color(red: 0.45, green: 0.85, blue: 0.25), Color(red: 0.10, green: 0.75, blue: 0.55)]
    case .gbc: return [Color(red: 0.62, green: 0.36, blue: 1.0), Color(red: 0.24, green: 0.66, blue: 1.0)]
    case .gba: return [Color(red: 0.40, green: 0.30, blue: 1.0), Color(red: 1.0, green: 0.30, blue: 0.62)]
    case .snes: return [Color(red: 0.62, green: 0.36, blue: 1.0), Color(red: 1.0, green: 0.30, blue: 0.62)]
    case .md: return [Color(red: 0.12, green: 0.55, blue: 1.0), Color(red: 0.20, green: 0.85, blue: 0.90)]
    case .ps1: return [Color(red: 1.0, green: 0.72, blue: 0.10), Color(red: 1.0, green: 0.38, blue: 0.20)]
    }
  }

  /// 확장자로 기종 찾기. .bin 은 같은 폴더의 .cue 가 가리키면 PS1 데이터(게임 아님), 아니면 메가드라이브.
  static func from(path: String) -> GameSystem? {
    let ext = (path as NSString).pathExtension.lowercased()
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

  static var allExtensions: [String] { allCases.flatMap { $0.extensions } + ["bin", "zip"] }
}
