import SwiftUI
import UniformTypeIdentifiers

let bgGradient = LinearGradient(
  colors: [Color(red: 1.0, green: 0.93, blue: 0.97), Color(red: 0.93, green: 0.92, blue: 1.0), Color(red: 0.90, green: 0.96, blue: 1.0)],
  startPoint: .topLeading, endPoint: .bottomTrailing)
let accent = LinearGradient(
  colors: [Color(red: 1.0, green: 0.30, blue: 0.62), Color(red: 0.61, green: 0.36, blue: 1.0), Color(red: 0.24, green: 0.66, blue: 1.0)],
  startPoint: .leading, endPoint: .trailing)

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

struct LibraryView: View {
  @EnvironmentObject var model: AppModel
  @State private var dropping = false

  var body: some View {
    ZStack {
      bgGradient.ignoresSafeArea()
      VStack(alignment: .leading, spacing: 18) {
        HStack {
          Text("레트로 에뮬레이터")
            .font(.system(size: 30, weight: .heavy))
            .foregroundStyle(accent)
          Spacer()
          Button(action: pick) {
            Label("롬 추가", systemImage: "plus")
              .font(.system(size: 14, weight: .bold))
              .padding(.horizontal, 16).padding(.vertical, 9)
              .background(accent, in: Capsule())
              .foregroundColor(.white)
          }.buttonStyle(.plain)
        }
        if model.games.isEmpty {
          emptyState
        } else {
          ScrollView {
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 190), spacing: 16)], spacing: 16) {
              ForEach(model.games) { g in
                GameCard(game: g).onTapGesture { model.playing = g }
                  .contextMenu { Button("목록에서 지우기") { model.remove(g) } }
              }
            }.padding(.bottom, 20)
          }
        }
      }
      .padding(28)
      if dropping { RoundedRectangle(cornerRadius: 24).strokeBorder(accent, lineWidth: 4).padding(10).allowsHitTesting(false) }
    }
    .onDrop(of: [.fileURL], isTargeted: $dropping) { providers in
      for p in providers {
        _ = p.loadObject(ofClass: URL.self) { url, _ in
          if let url { DispatchQueue.main.async { model.add(urls: [url]) } }
        }
      }
      return true
    }
  }

  var emptyState: some View {
    VStack(spacing: 14) {
      Spacer()
      Image(systemName: "square.and.arrow.down").font(.system(size: 44, weight: .bold)).foregroundStyle(accent)
      Text("롬 파일을 끌어다 놓거나 롬 추가를 눌러요").font(.system(size: 17, weight: .semibold)).foregroundColor(.black.opacity(0.7))
      Text(GameSystem.allCases.map { $0.title }.joined(separator: " · "))
        .font(.system(size: 13)).foregroundColor(.black.opacity(0.5))
      Spacer()
    }.frame(maxWidth: .infinity)
  }

  func pick() {
    let p = NSOpenPanel()
    p.allowsMultipleSelection = true
    p.allowedContentTypes = GameSystem.allExtensions.compactMap { UTType(filenameExtension: $0) }
    if p.runModal() == .OK { model.add(urls: p.urls) }
  }
}

struct GameCard: View {
  let game: Game
  var body: some View {
    VStack(alignment: .leading, spacing: 6) {
      Spacer(minLength: 0)
      Text(game.system.title).font(.system(size: 12, weight: .bold)).foregroundColor(.white.opacity(0.9))
      Text(game.title).font(.system(size: 17, weight: .heavy)).foregroundColor(.white).lineLimit(2)
    }
    .frame(maxWidth: .infinity, minHeight: 110, alignment: .leading)
    .padding(16)
    .background(LinearGradient(colors: game.system.gradient, startPoint: .topLeading, endPoint: .bottomTrailing),
                in: RoundedRectangle(cornerRadius: 20))
    .shadow(color: game.system.gradient[0].opacity(0.35), radius: 10, y: 5)
    .contentShape(RoundedRectangle(cornerRadius: 20))
  }
}

// MARK: - 게임 화면

final class GameNSView: NSView {
  var onKey: ((UInt16, Bool) -> Void)?
  override var acceptsFirstResponder: Bool { true }
  override func viewDidMoveToWindow() { window?.makeFirstResponder(self) }
  override func keyDown(with e: NSEvent) { if !e.isARepeat { onKey?(e.keyCode, true) } }
  override func keyUp(with e: NSEvent) { onKey?(e.keyCode, false) }
  override func flagsChanged(with e: NSEvent) {}
}

struct GameSurface: NSViewRepresentable {
  let emu: Emulator
  let onSpecial: (UInt16) -> Void

  func makeNSView(context: Context) -> GameNSView {
    let v = GameNSView()
    v.wantsLayer = true
    v.layer?.backgroundColor = NSColor.black.cgColor
    v.layer?.magnificationFilter = .nearest
    v.layer?.minificationFilter = .linear
    v.layer?.contentsGravity = .resizeAspect
    v.onKey = { code, down in
      if let b = Emulator.bit(forKeyCode: code) {
        if down { emu.keyMask |= b } else { emu.keyMask &= ~b }
      } else if down { onSpecial(code) }
    }
    emu.onFrame = { [weak v] img, _ in v?.layer?.contents = img }
    return v
  }
  func updateNSView(_ v: GameNSView, context: Context) {}
}

struct PlayView: View {
  let game: Game
  @EnvironmentObject var model: AppModel
  @State private var emu = Emulator()
  @State private var toast: String?
  @State private var toastGen = 0

  var body: some View {
    ZStack(alignment: .top) {
      Color.black.ignoresSafeArea()
      GameSurface(emu: emu) { code in
        switch code {
        case 53: quit()                 // Esc
        case 122: emu.saveState()       // F1
        case 120: emu.loadState()       // F2
        default: break
        }
      }
      if let t = toast {
        Text(t).font(.system(size: 14, weight: .bold)).foregroundColor(.white)
          .padding(.horizontal, 16).padding(.vertical, 8)
          .background(accent, in: Capsule()).padding(.top, 16)
          .transition(.opacity)
      }
    }
    .onAppear {
      emu.onToast = { show($0) }
      if let err = emu.start(rom: game.path, system: game.system) {
        model.playing = nil
        model.error = err
      } else {
        show("Esc 나가기 · F1 저장 · F2 불러오기")
      }
    }
    .onDisappear { emu.stop() }
  }

  func quit() { model.playing = nil }

  func show(_ s: String) {
    toastGen += 1
    let g = toastGen
    withAnimation { toast = s }
    DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
      if g == toastGen { withAnimation { toast = nil } }
    }
  }
}
