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
  @State private var showSync = false
  @State private var dropping = false
  /// 잠금 풀기 경고를 띄울 게임
  @State private var unlockTarget: Game?
  /// 잠긴 게임을 눌렀을 때 안내
  @State private var lockedTap: Game?

  var body: some View {
    ZStack {
      bgGradient.ignoresSafeArea()
      VStack(alignment: .leading, spacing: 18) {
        HStack {
          Text("레트로 에뮬레이터")
            .font(.system(size: 30, weight: .heavy))
            .foregroundStyle(accent)
          Spacer()
          Button { showSync = true } label: {
            Label(Sync.enabled ? "동기화 켜짐" : "동기화", systemImage: "arrow.triangle.2.circlepath")
              .font(.system(size: 14, weight: .bold))
              .padding(.horizontal, 16).padding(.vertical, 9)
              .background(Color.white.opacity(0.85), in: Capsule())
              .foregroundColor(Color(red: 0.36, green: 0.2, blue: 0.75))
          }.buttonStyle(.plain)
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
                GameCard(game: g).onTapGesture { if g.isLocked { lockedTap = g } else { model.playing = g } }
                  .contextMenu {
                    if g.isLocked { Button("잠금 풀기") { unlockTarget = g } }
                    else { Button("잠그기") { model.setLocked(g, true) } }
                    Button("목록에서 지우기") { model.remove(g) }
                  }
              }
            }.padding(.bottom, 20)
          }
        }
      }
      .padding(28)
      if dropping { RoundedRectangle(cornerRadius: 24).strokeBorder(accent, lineWidth: 4).padding(10).allowsHitTesting(false) }
      if showSync {
        Color.black.opacity(0.45).ignoresSafeArea().onTapGesture { showSync = false }
        SyncView(roms: model.games.map { $0.path }) { showSync = false }
      }
    }
    .alert("\(unlockTarget?.title ?? "") 잠금을 풀까요?", isPresented: Binding(get: { unlockTarget != nil }, set: { if !$0 { unlockTarget = nil } })) {
      Button("잠금 풀기", role: .destructive) { if let g = unlockTarget { model.setLocked(g, false) }; unlockTarget = nil }
      Button("취소", role: .cancel) { unlockTarget = nil }
    } message: { Text(lockWarning) }
    .alert("잠긴 게임이에요", isPresented: Binding(get: { lockedTap != nil }, set: { if !$0 { lockedTap = nil } })) {
      Button("확인", role: .cancel) { lockedTap = nil }
    } message: { Text("켜려면 오른쪽 클릭해서 잠금 풀기를 눌러요.") }
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

/// 잠금을 풀 때 보여 주는 경고(네 앱 모두 같은 문구)
let lockWarning = "예전엔 게임이 비싸서 하나를 끝까지 파고들었죠. 잠금을 풀면 그 맛이 흐려져요. 지금 하는 게임을 충분히 즐긴 뒤에 푸는 걸 추천해요."

struct GameCard: View {
  let game: Game
  var body: some View {
    ZStack(alignment: .bottomLeading) {
      // 게임기 사진: 오른쪽 위, 글자와 안 겹치게
      if let img = game.system.photo {
        GeometryReader { g in
          Image(nsImage: img).resizable().interpolation(.high).aspectRatio(contentMode: .fit)
            .frame(width: g.size.width * (game.system.tallPhoto ? 0.40 : 0.70), height: game.system.tallPhoto ? 92 : 78, alignment: .topTrailing)
            .shadow(color: .black.opacity(0.18), radius: 6, y: 3)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
        }
      }
      if game.isLocked {
        Label("잠김", systemImage: "lock.fill")
          .font(.system(size: 12, weight: .heavy)).foregroundColor(.white)
          .padding(.horizontal, 10).padding(.vertical, 5)
          .background(Color.black.opacity(0.35), in: Capsule())
          .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
      }
      VStack(alignment: .leading, spacing: 4) {
        Text(game.system.title).font(.system(size: 12, weight: .bold)).foregroundColor(.white.opacity(0.9))
        Text(game.title).font(.system(size: 17, weight: .heavy)).foregroundColor(.white).lineLimit(2)
      }
    }
    .frame(maxWidth: .infinity, minHeight: 150, maxHeight: 150, alignment: .leading)
    .padding(14)
    .background(LinearGradient(colors: game.system.gradient, startPoint: .topLeading, endPoint: .bottomTrailing),
                in: RoundedRectangle(cornerRadius: 20))
    .saturation(game.isLocked ? 0.35 : 1)
    .shadow(color: game.system.gradient[0].opacity(0.35), radius: 10, y: 5)
    .contentShape(RoundedRectangle(cornerRadius: 20))
  }
}

// MARK: - 게임 화면

final class GameNSView: NSView {
  var onKey: ((UInt16, Bool) -> Void)?
  /// 마우스로 누른 자리(게임 그림 기준 0~1, 위가 0)
  var onPointer: ((Double, Double, Bool) -> Void)?
  /// 화면 비율(가로/세로). 코어가 알려 준 값이라 그림 픽셀 크기와 다를 수 있다(N64 640x240 은 4:3).
  var aspect: Double = 4.0 / 3.0 { didSet { if abs(aspect - oldValue) > 0.001 { needsLayout = true } } }
  /// 화면 돌리기(시계 방향 90도 단위). 옆으로 드는 DS 게임(러브플러스)용.
  var rotation = 0 { didSet { if rotation != oldValue { needsLayout = true } } }
  private var shownAspect: Double { rotation % 2 == 1 ? 1 / aspect : aspect }
  let screen = CALayer()
  func show(_ img: CGImage, aspect a: Double) {
    CATransaction.begin(); CATransaction.setDisableActions(true)
    screen.contents = img
    CATransaction.commit()
    aspect = a > 0.1 ? a : Double(img.width) / Double(max(img.height, 1))
  }
  /// 비율을 지켜 가운데 맞춘 그림 자리
  var screenRect: CGRect {
    let b = bounds.size
    let a = shownAspect
    var w = b.width, h = w / a
    if h > b.height { h = b.height; w = h * a }
    return CGRect(x: (b.width - w) / 2, y: (b.height - h) / 2, width: w, height: h)
  }
  override func layout() {
    super.layout()
    CATransaction.begin(); CATransaction.setDisableActions(true)
    let r = screenRect
    // 돌리기 전 크기로 놓고 가운데를 축으로 돌린다(맥 레이어는 양수 각도가 반시계라서 음수)
    screen.transform = CATransform3DIdentity
    screen.bounds = CGRect(x: 0, y: 0, width: rotation % 2 == 1 ? r.height : r.width, height: rotation % 2 == 1 ? r.width : r.height)
    screen.position = CGPoint(x: r.midX, y: r.midY)
    screen.transform = CATransform3DMakeRotation(-CGFloat(rotation) * .pi / 2, 0, 0, 1)
    CATransaction.commit()
  }
  private func pointer(_ e: NSEvent, _ down: Bool) {
    let p = convert(e.locationInWindow, from: nil), r = screenRect
    let u = (p.x - r.minX) / r.width, v = 1 - (p.y - r.minY) / r.height
    // 돌린 화면에서 누른 자리를 원래 그림 자리로
    let (x, y): (Double, Double) = rotation == 1 ? (v, 1 - u) : rotation == 2 ? (1 - u, 1 - v) : rotation == 3 ? (1 - v, u) : (u, v)
    onPointer?(x, y, down && x >= 0 && x <= 1 && y >= 0 && y <= 1)
  }
  override func mouseDown(with e: NSEvent) { pointer(e, true) }
  override func mouseDragged(with e: NSEvent) { pointer(e, true) }
  override func mouseUp(with e: NSEvent) { pointer(e, false) }
  override var acceptsFirstResponder: Bool { true }
  override func viewDidMoveToWindow() { window?.makeFirstResponder(self) }
  override func keyDown(with e: NSEvent) { if !e.isARepeat { onKey?(e.keyCode, true) } }
  override func keyUp(with e: NSEvent) { onKey?(e.keyCode, false) }
  override func flagsChanged(with e: NSEvent) {}
}

struct GameSurface: NSViewRepresentable {
  let emu: Emulator
  /// 저장 목록이 열려 있으면 게임 키를 안 보낸다
  let panelOpen: Bool
  var rotation = 0
  let onSpecial: (UInt16) -> Void

  func makeNSView(context: Context) -> GameNSView {
    let v = GameNSView()
    v.wantsLayer = true
    v.layer?.backgroundColor = NSColor.black.cgColor
    v.screen.magnificationFilter = .nearest
    v.screen.minificationFilter = .linear
    v.screen.contentsGravity = .resize
    v.layer?.addSublayer(v.screen)
    v.onPointer = { x, y, down in emu.setPointer(x, y, down && !context.coordinator.panelOpen) }
    v.onKey = { code, down in
      if let a = emu.keyLookup[code], !KeyMap.reserved.contains(code), !context.coordinator.panelOpen {
        if a >= 20 {
          let r = UInt32(1) << UInt32(a - 20)
          if down { emu.leftKeys |= r } else { emu.leftKeys &= ~r }
        } else if a >= 16 {
          let r = UInt32(1) << UInt32(a - 16)
          if down { emu.rightKeys |= r } else { emu.rightKeys &= ~r }
        } else {
          let b = UInt32(1) << UInt32(a)
          if down { emu.keyMask |= b } else { emu.keyMask &= ~b }
        }
      } else if down { onSpecial(code) }
    }
    emu.onFrame = { [weak v] img, a in v?.show(img, aspect: a) }
    return v
  }
  func updateNSView(_ v: GameNSView, context: Context) {
    context.coordinator.panelOpen = panelOpen
    v.rotation = rotation
    // 목록의 버튼을 누르면 키 입력을 뺏길 수 있어서 다시 가져온다
    DispatchQueue.main.async { if v.window?.firstResponder !== v { v.window?.makeFirstResponder(v) } }
  }
  func makeCoordinator() -> Coord { Coord() }
  final class Coord { var panelOpen = false }
}

struct PlayView: View {
  let game: Game
  @EnvironmentObject var model: AppModel
  @State private var emu = Emulator()
  @State private var toast: String?
  @State private var toastGen = 0
  @State private var panel = false
  /// Esc 로 여는 일시정지 메뉴와 고른 줄
  @State private var menu = false
  @State private var menuSel = 0
  /// 화면 돌리기(게임마다 기억)
  @State private var rotation = 0
  private var rotKey: String { "rotation." + (game.path as NSString).lastPathComponent }
  /// 조작법 화면
  @State private var controls = false
  @State private var ctlSel = 0
  @State private var capture: Int?
  @State private var keymap: [Int: UInt16] = [:]
  @State private var ctlRows: [(Int, String)] = []
  @State private var slots: [SaveSlot] = []
  /// F1/F2 가 쓰는 슬롯
  @State private var quickSlot = "1"
  @State private var canSave = true
  @State private var syncing = false

  var body: some View {
    ZStack(alignment: .top) {
      Color.black.ignoresSafeArea()
      GameSurface(emu: emu, panelOpen: panel || menu || controls, rotation: rotation) { code in
        if controls { controlsKey(code); return }
        if menu {
          switch code {
          case 53: setMenu(false)                                  // Esc: 이어하기
          case 126: menuSel = max(0, menuSel - 1)                  // 위
          case 125: menuSel = min(PauseMenu.count - 1, menuSel + 1)  // 아래
          case 36, 76, 49: pickMenu(menuSel)                       // Enter, 스페이스
          default: break
          }
          return
        }
        switch code {
        case 53: if panel { setPanel(false) } else { setMenu(true) }  // Esc
        case 122: emu.save(slot: quickSlot)  // F1
        case 120: emu.load(slot: quickSlot)  // F2
        case 99: setPanel(!panel)            // F3
        default: break
        }
      }
      if menu {
        PauseMenu(sel: $menuSel, rotation: rotation, onPick: pickMenu, onClose: { setMenu(false) })
          .transition(.opacity)
      }
      if controls {
        ControlsPanel(system: game.system, rows: ctlRows, map: keymap, sel: $ctlSel, capturing: capture,
                      onPick: { i in ctlSel = i; capture = ctlRows[i].0 },
                      onReset: { KeyMap.reset(game.system); applyKeys(KeyMap.load(game.system)); show("기본 키로 돌렸어요") },
                      onClose: closeControls)
          .transition(.opacity)
      }
      if panel {
        SlotPanel(slots: slots, quickSlot: quickSlot, canSave: canSave,
                  onSave: { id in quickSlot = id; emu.save(slot: id) { slots = emu.slots() } },
                  onLoad: { id in if !SaveSlot.isAutoId(id) { quickSlot = id }; emu.load(slot: id); setPanel(false) },
                  onDelete: { id in emu.delete(slot: id); slots = emu.slots() },
                  onClose: { setPanel(false) })
          .transition(.opacity)
      }
      if let t = toast {
        Text(t).font(.system(size: 14, weight: .bold)).foregroundColor(.white)
          .padding(.horizontal, 16).padding(.vertical, 8)
          .background(accent, in: Capsule()).padding(.top, 16)
          .transition(.opacity)
          .allowsHitTesting(false)
      }
    }
    .onAppear {
      rotation = UserDefaults.standard.integer(forKey: rotKey)
      emu.onToast = { show($0) }
      // 동기화를 켰으면 다른 기기 저장부터 받고 시작한다
      guard Sync.enabled else { return begin() }
      syncing = true
      let rom = (game.path as NSString).lastPathComponent
      DispatchQueue.global(qos: .userInitiated).async {
        let r = Sync.syncGame(savesDir: Emulator.savesDir, romFile: rom, pull: true)
        DispatchQueue.main.async {
          syncing = false
          guard model.playing?.path == game.path else { return }  // 기다리는 동안 나감
          begin()
          if r == nil { show("동기화 서버에 못 닿아서 이 기기 저장으로 시작해요") }
        }
      }
    }
    .overlay { if syncing { syncOverlay } }
    .onDisappear { emu.stop() }
  }

  var syncOverlay: some View {
    VStack(spacing: 10) {
      ProgressView().controlSize(.large)
      Text("다른 기기 저장을 받는 중").font(.system(size: 15, weight: .bold)).foregroundColor(.white)
    }
  }

  func begin() {
      if let err = emu.start(rom: game.path, system: game.system) {
        model.playing = nil
        model.error = err
      } else if emu.slots().first(where: { $0.isAuto })?.date != nil {
        // 지난번에 나갈 때 자동 저장한 게 있으면 바로 고를 수 있게 목록부터 연다
        setPanel(true)
      } else {
        show("Esc 메뉴 · F1 저장 · F2 불러오기 · F3 저장 목록")
      }
  }

  func setPanel(_ open: Bool) {
    if open { slots = emu.slots(); canSave = emu.hasRun }
    emu.setPaused(open)
    withAnimation(.easeOut(duration: 0.15)) { panel = open }
  }

  func setMenu(_ open: Bool) {
    menuSel = 0
    emu.setPaused(open)
    withAnimation(.easeOut(duration: 0.15)) { menu = open }
  }

  func pickMenu(_ i: Int) {
    switch i {
    case 0: setMenu(false)
    case 1: menu = false; setPanel(true)
    case 2: openControls()
    case 3:
      rotation = (rotation + 1) % 4
      UserDefaults.standard.set(rotation, forKey: rotKey)
    default: menu = false; quit()
    }
  }

  func openControls() {
    keymap = KeyMap.load(game.system)
    ctlRows = KeyMap.rows(game.system)
    ctlSel = 0; capture = nil
    withAnimation(.easeOut(duration: 0.15)) { menu = false; controls = true }
  }

  /// 조작법을 닫으면 일시정지 메뉴로 돌아간다
  func closeControls() {
    capture = nil
    withAnimation(.easeOut(duration: 0.15)) { controls = false; menu = true }
  }

  func applyKeys(_ m: [Int: UInt16]) {
    keymap = m
    emu.keyLookup = KeyMap.lookup(m)
    emu.keyMask = 0; emu.rightKeys = 0; emu.leftKeys = 0
  }

  func controlsKey(_ code: UInt16) {
    if let a = capture {
      capture = nil
      if code == 53 { return }   // Esc: 취소
      if KeyMap.reserved.contains(code) { show("그 키는 메뉴용이라 못 써요"); return }
      var m = keymap
      KeyMap.assign(code, to: a, in: &m)
      KeyMap.save(m, for: game.system)
      applyKeys(m)
      return
    }
    switch code {
    case 53: closeControls()
    case 126: ctlSel = max(0, ctlSel - 2)
    case 125: ctlSel = min(ctlRows.count - 1, ctlSel + 2)
    case 123: ctlSel = max(0, ctlSel - 1)
    case 124: ctlSel = min(ctlRows.count - 1, ctlSel + 1)
    case 36, 76, 49: if ctlRows.indices.contains(ctlSel) { capture = ctlRows[ctlSel].0 }
    default: break
    }
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

// MARK: - 일시정지 메뉴

struct PauseMenu: View {
  static let count = 5
  static let rotNames = ["그대로", "오른쪽으로", "거꾸로", "왼쪽으로"]
  @Binding var sel: Int
  var rotation = 0
  var items: [String] { ["이어하기", "저장 / 불러오기", "조작법", "화면 돌리기: " + PauseMenu.rotNames[rotation], "나가기"] }
  let onPick: (Int) -> Void
  let onClose: () -> Void

  var body: some View {
    ZStack {
      Color.black.opacity(0.55).ignoresSafeArea().onTapGesture(perform: onClose)
      VStack(alignment: .leading, spacing: 12) {
        HStack {
          Text("일시정지").font(.system(size: 22, weight: .heavy)).foregroundStyle(accent)
          Spacer()
          Text("Esc 이어하기").font(.system(size: 12, weight: .medium)).foregroundColor(.black.opacity(0.45))
        }
        .padding(.bottom, 4)
        ForEach(Array(items.enumerated()), id: \.offset) { i, t in
          Button { onPick(i) } label: {
            Text(t).font(.system(size: 16, weight: .bold))
              .frame(maxWidth: .infinity).padding(.vertical, 13)
              .foregroundColor(i == sel ? .white : Color(red: 0.36, green: 0.2, blue: 0.75))
              .background(i == sel ? AnyShapeStyle(accent) : AnyShapeStyle(Color(red: 0.93, green: 0.91, blue: 1.0)), in: Capsule())
              .contentShape(Capsule())
          }
          .buttonStyle(.plain)
          .onHover { if $0 { sel = i } }
        }
      }
      .padding(22)
      .frame(width: 340)
      .background(bgGradient, in: RoundedRectangle(cornerRadius: 24))
      .shadow(color: .black.opacity(0.3), radius: 30, y: 10)
    }
  }
}

// MARK: - 저장 목록

struct SlotPanel: View {
  let slots: [SaveSlot]
  let quickSlot: String
  let canSave: Bool
  let onSave: (String) -> Void
  let onLoad: (String) -> Void
  let onDelete: (String) -> Void
  let onClose: () -> Void
  @State private var overwrite: String?

  var body: some View {
    ZStack {
      Color.black.opacity(0.55).ignoresSafeArea().onTapGesture(perform: onClose)
      VStack(alignment: .leading, spacing: 14) {
        HStack {
          Text("저장 / 불러오기").font(.system(size: 22, weight: .heavy)).foregroundStyle(accent)
          Spacer()
          Text("Esc 닫기 · F1/F2 는 마지막에 고른 슬롯")
            .font(.system(size: 12, weight: .medium)).foregroundColor(.black.opacity(0.45))
        }
        ScrollView {
          LazyVGrid(columns: [GridItem(.adaptive(minimum: 170), spacing: 12)], spacing: 12) {
            ForEach(slots) { s in
              SlotCard(slot: s, selected: s.id == quickSlot, canSave: canSave,
                       onSave: { if s.date != nil { overwrite = s.id } else { onSave(s.id) } },
                       onLoad: { onLoad(s.id) })
                .contextMenu { if s.date != nil { Button("지우기") { onDelete(s.id) } } }
            }
          }.padding(2)
        }
      }
      .padding(22)
      .frame(maxWidth: 820, maxHeight: 600)
      .background(bgGradient, in: RoundedRectangle(cornerRadius: 24))
      .shadow(color: .black.opacity(0.3), radius: 30, y: 10)
      .padding(28)
    }
    .alert("덮어쓸까요?", isPresented: Binding(get: { overwrite != nil }, set: { if !$0 { overwrite = nil } })) {
      Button("덮어쓰기") { if let id = overwrite { onSave(id) }; overwrite = nil }
      Button("취소", role: .cancel) { overwrite = nil }
    } message: { Text("\(SaveSlot.name(overwrite ?? "1"))에 있던 저장이 지워져요.") }
  }
}

struct SlotCard: View {
  let slot: SaveSlot
  let selected: Bool
  let canSave: Bool
  let onSave: () -> Void
  let onLoad: () -> Void

  static let fmt: DateFormatter = {
    let f = DateFormatter(); f.locale = Locale(identifier: "ko_KR"); f.dateFormat = "M월 d일 a h:mm"; return f
  }()

  var body: some View {
    VStack(alignment: .leading, spacing: 8) {
      ZStack {
        Color.black
        if let t = slot.thumb {
          Image(nsImage: t).resizable().interpolation(.none).aspectRatio(contentMode: .fit)
        } else {
          Text(slot.date == nil ? "비어 있음" : "화면 없음").font(.system(size: 13, weight: .semibold)).foregroundColor(.white.opacity(0.5))
        }
      }
      .aspectRatio(4 / 3, contentMode: .fit)
      .clipShape(RoundedRectangle(cornerRadius: 12))
      HStack(alignment: .firstTextBaseline) {
        Text(SaveSlot.name(slot.id)).font(.system(size: 14, weight: .heavy)).foregroundColor(.black.opacity(0.8))
        Spacer(minLength: 4)
        Text(slot.date.map { SlotCard.fmt.string(from: $0) } ?? "")
          .font(.system(size: 11, weight: .medium)).foregroundColor(.black.opacity(0.5)).lineLimit(1)
      }
      HStack(spacing: 8) {
        if !slot.isAuto { pill("저장", filled: false, action: onSave).disabled(!canSave).opacity(canSave ? 1 : 0.35) }
        pill("불러오기", filled: true, action: onLoad).disabled(slot.date == nil).opacity(slot.date == nil ? 0.35 : 1)
      }
    }
    .padding(10)
    .background(Color.white.opacity(0.85), in: RoundedRectangle(cornerRadius: 16))
    .overlay(RoundedRectangle(cornerRadius: 16).strokeBorder(accent, lineWidth: selected ? 3 : 0))
  }

  func pill(_ t: String, filled: Bool, action: @escaping () -> Void) -> some View {
    Button(action: action) {
      Text(t).font(.system(size: 13, weight: .bold))
        .frame(maxWidth: .infinity).padding(.vertical, 7)
        .foregroundColor(filled ? .white : Color(red: 0.36, green: 0.2, blue: 0.75))
        .background(filled ? AnyShapeStyle(accent) : AnyShapeStyle(Color(red: 0.93, green: 0.91, blue: 1.0)), in: Capsule())
        .contentShape(Capsule())
    }.buttonStyle(.plain)
  }
}
