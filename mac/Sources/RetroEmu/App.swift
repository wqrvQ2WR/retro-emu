import SwiftUI
import UniformTypeIdentifiers

struct Game: Codable, Identifiable, Hashable {
  var path: String
  var title: String
  var system: GameSystem
  /// 잠긴 게임은 목록에서 눌러도 안 켜진다(예전 library.json 엔 없어서 옵셔널)
  var locked: Bool?
  var id: String { path }
  var isLocked: Bool { locked ?? false }
}

@MainActor
final class AppModel: ObservableObject {
  @Published var games: [Game] = []
  @Published var playing: Game?
  @Published var error: String?

  private var file: URL { Emulator.supportDir.appendingPathComponent("library.json") }

  init() {
    if let d = try? Data(contentsOf: file), let g = try? JSONDecoder().decode([Game].self, from: d) {
      games = g.filter { FileManager.default.fileExists(atPath: $0.path) }
    }
  }

  /// zip 이면 풀어서 안의 파일들을 돌려준다(경로 정보는 버리고 이름만 씀).
  private func expand(_ u: URL) -> [URL] {
    guard u.pathExtension.lowercased() == "zip" else { return [u] }
    let dir = Emulator.supportDir.appendingPathComponent("roms").appendingPathComponent(u.deletingPathExtension().lastPathComponent)
    try? FileManager.default.removeItem(at: dir)
    try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/usr/bin/unzip")
    p.arguments = ["-o", "-j", "-qq", u.path, "-d", dir.path]
    p.standardOutput = FileHandle.nullDevice; p.standardError = FileHandle.nullDevice
    do { try p.run(); p.waitUntilExit() } catch { return [] }
    return ((try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil)) ?? [])
      .filter { GameSystem.from(path: $0.path) != nil }
  }

  func add(urls: [URL]) {
    var found = false
    for u in urls.flatMap({ expand($0) }) {
      found = true
      guard let sys = GameSystem.from(path: u.path), !games.contains(where: { $0.path == u.path }) else { continue }
      let title = u.deletingPathExtension().lastPathComponent
      games.append(Game(path: u.path, title: title, system: sys))
    }
    games.sort { $0.title.localizedCompare($1.title) == .orderedAscending }
    save()
    if !found && !urls.isEmpty { error = "열 수 있는 롬이 없어요" }
  }

  func setLocked(_ g: Game, _ on: Bool) {
    guard let i = games.firstIndex(where: { $0.path == g.path }) else { return }
    games[i].locked = on ? true : nil
    save()
  }

  func remove(_ g: Game) { games.removeAll { $0 == g }; save() }

  func open(path: String) {
    guard let sys = GameSystem.from(path: path) else { error = "지원하지 않는 파일이에요"; return }
    playing = Game(path: path, title: ((path as NSString).lastPathComponent as NSString).deletingPathExtension, system: sys)
  }

  private func save() { try? JSONEncoder().encode(games).write(to: file) }
}

@main
struct RetroEmuApp: App {
  @StateObject private var model = AppModel()

  var body: some Scene {
    WindowGroup("레트로 에뮬레이터") {
      ContentView()
        .environmentObject(model)
        .frame(minWidth: 720, minHeight: 520)
        .onAppear {
          let a = CommandLine.arguments
          if let i = a.firstIndex(of: "--rom"), i + 1 < a.count { model.open(path: a[i + 1]) }
          if let i = a.firstIndex(of: "--add") { model.add(urls: a[(i + 1)...].filter { !$0.hasPrefix("--") }.map { URL(fileURLWithPath: $0) }) }
        }
    }
    .defaultSize(width: 960, height: 680)
  }
}
