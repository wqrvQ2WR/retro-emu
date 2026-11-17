import SwiftUI
import UniformTypeIdentifiers

struct Game: Identifiable, Hashable {
  var path: String
  var title: String
  var system: GameSystem
  var id: String { path }
}

/// 게임 목록 = Documents/roms 안의 롬 파일. 따로 목록 파일을 두지 않고 열 때마다 폴더를 훑는다.
/// 파일 앱으로 Documents 에 바로 넣은 롬도 roms 로 옮겨서 보여준다.
@MainActor
final class AppModel: ObservableObject {
  @Published var games: [Game] = []
  @Published var playing: Game?
  @Published var error: String?

  static var romsDir: URL {
    let u = Emulator.supportDir.appendingPathComponent("roms")
    try? FileManager.default.createDirectory(at: u, withIntermediateDirectories: true)
    return u
  }

  init() { rescan() }

  func rescan() {
    let fm = FileManager.default
    let root = Emulator.supportDir, roms = AppModel.romsDir
    // Documents 맨 위에 놓인 롬·zip 은 roms 로 가져온다
    for u in (try? fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil)) ?? [] {
      let ext = u.pathExtension.lowercased()
      guard GameSystem.allExtensions.contains(ext) else { continue }
      _ = importFile(u, move: true)
    }
    var list: [Game] = []
    let all = (fm.enumerator(at: roms, includingPropertiesForKeys: nil)?.allObjects as? [URL]) ?? []
    for u in all {
      guard let sys = GameSystem.from(path: u.path) else { continue }
      list.append(Game(path: u.path, title: u.deletingPathExtension().lastPathComponent, system: sys))
    }
    games = list.sorted { $0.title.localizedCompare($1.title) == .orderedAscending }
  }

  /// 롬 한 개를 roms 로 복사(또는 이동). zip 이면 roms/<이름>/ 에 푼다. 롬이 하나라도 있으면 true.
  private func importFile(_ u: URL, move: Bool) -> Bool {
    let fm = FileManager.default
    if u.pathExtension.lowercased() == "zip" {
      let dir = AppModel.romsDir.appendingPathComponent(u.deletingPathExtension().lastPathComponent)
      try? fm.removeItem(at: dir)
      try? fm.createDirectory(at: dir, withIntermediateDirectories: true)
      let files = Zip.extract(u, to: dir)
      if move { try? fm.removeItem(at: u) }
      let ok = files.contains { GameSystem.from(path: $0.path) != nil }
      if !ok { try? fm.removeItem(at: dir) }
      return ok
    }
    let dst = AppModel.romsDir.appendingPathComponent(u.lastPathComponent)
    if dst.standardizedFileURL == u.standardizedFileURL { return true }
    try? fm.removeItem(at: dst)
    do {
      if move { try fm.moveItem(at: u, to: dst) } else { try fm.copyItem(at: u, to: dst) }
    } catch { return false }
    return GameSystem.allExtensions.contains(u.pathExtension.lowercased())
  }

  /// 파일 고르기로 고른 것들. 보안 범위 URL 이라 열고 닫아 준다.
  func add(urls: [URL]) {
    var found = false
    for u in urls {
      let scoped = u.startAccessingSecurityScopedResource()
      defer { if scoped { u.stopAccessingSecurityScopedResource() } }
      if importFile(u, move: false) { found = true }
    }
    rescan()
    if !found && !urls.isEmpty { error = "열 수 있는 롬이 없어요" }
  }

  /// 롬 파일을 지운다(저장은 남긴다)
  func remove(_ g: Game) {
    let fm = FileManager.default, u = URL(fileURLWithPath: g.path)
    try? fm.removeItem(at: u)
    let dir = u.deletingLastPathComponent()
    if dir.standardizedFileURL != AppModel.romsDir.standardizedFileURL,
       ((try? fm.contentsOfDirectory(atPath: dir.path)) ?? []).allSatisfy({ GameSystem.from(path: dir.path + "/" + $0) == nil }) {
      try? fm.removeItem(at: dir)
    }
    rescan()
  }

  func open(path: String) {
    guard let sys = GameSystem.from(path: path) else { error = "지원하지 않는 파일이에요"; return }
    playing = Game(path: path, title: ((path as NSString).lastPathComponent as NSString).deletingPathExtension, system: sys)
  }
}

@main
struct RetroEmuApp: App {
  @StateObject private var model = AppModel()
  @Environment(\.scenePhase) private var phase

  var body: some Scene {
    WindowGroup {
      ContentView()
        .environmentObject(model)
        .onAppear {
          TestHooks.run()
          // 점검용: --add <파일...>, --rom <파일>
          let a = CommandLine.arguments
          if let i = a.firstIndex(of: "--add") { model.add(urls: a[(i + 1)...].prefix { !$0.hasPrefix("--") }.map { URL(fileURLWithPath: $0) }) }
          if let i = a.firstIndex(of: "--rom"), i + 1 < a.count { model.open(path: a[i + 1]) }
          // --leave <초>: 그 뒤에 게임에서 나간다(나갈 때 자동 저장·동기화 점검용)
          if let i = a.firstIndex(of: "--leave"), i + 1 < a.count, let t = Double(a[i + 1]) {
            DispatchQueue.main.asyncAfter(deadline: .now() + t) { model.playing = nil }
          }
        }
        .onChange(of: phase) { p in if p == .active && model.playing == nil { model.rescan() } }
    }
  }
}

struct ContentView: View {
  @EnvironmentObject var model: AppModel
  var body: some View {
    ZStack {
      if let g = model.playing {
        PlayView(game: g).id(g.path)
      } else {
        LibraryView()
      }
    }
    .alert("열 수 없어요", isPresented: Binding(get: { model.error != nil }, set: { if !$0 { model.error = nil } })) {
      Button("확인") { model.error = nil }
    } message: { Text(model.error ?? "") }
  }
}
