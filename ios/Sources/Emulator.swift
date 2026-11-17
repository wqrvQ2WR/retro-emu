import AVFoundation
import CoreAudio
import GameController
import UIKit

/// 코어 한 개를 돌리는 쪽. 모든 코어 호출은 직렬 큐 하나에서만 한다. (맥판 Emulator.swift 를 iOS 에 맞게 옮김)
final class Emulator {
  private let queue = DispatchQueue(label: "retro.emu", qos: .userInteractive)
  private var timer: DispatchSourceTimer?
  private var engine: AVAudioEngine?
  private var lastSerial: UInt64 = 0
  private var sramPath = ""
  /// saves/<롬 파일 이름>. 슬롯 파일은 여기에 .<슬롯>.state / .<슬롯>.png 를 붙인다.
  private var base = ""
  private var sramTick = 0
  /// 롬 파일 이름(동기화에서 저장 파일을 고르는 기준)
  private(set) var romFile = ""
  static var savesDir: String { supportDir.appendingPathComponent("saves").path }
  /// 저장 목록을 보는 동안, 앱이 뒤로 간 동안 멈춘다(큐에서만 읽고 씀)
  private var panelPaused = false
  private var bgPaused = false
  /// 이번에 켜고 나서 돈 프레임 수. 거의 안 했으면 나갈 때 자동 저장을 덮어쓰지 않는다.
  private var framesRun = 0
  /// 패드 셀렉트+스타트, 홈 버튼, 키보드 Esc 를 한 번만 알리려고 앞 상태를 기억
  private var menuHeld = false

  /// 터치 버튼 입력 비트마스크(메인 스레드에서 쓰고 큐에서 읽음)
  var touchMask: UInt32 = 0
  /// 터치 스틱(아날로그 기종) 값: -1~1, 아래·오른쪽이 +
  var touchStick = CGPoint.zero
  private var system: GameSystem = .nes
  /// 새 영상이 나오면 호출(메인 스레드)
  var onFrame: ((CGImage, Double) -> Void)?
  var onToast: ((String) -> Void)?
  /// 패드나 키보드로 메뉴를 부를 때(메인 스레드)
  var onMenu: (() -> Void)?
  /// 게임패드 버튼이 처음 눌렸을 때(메인 스레드). 그때 터치 버튼을 숨긴다.
  var onPadUsed: (() -> Void)?
  private var padSeen = false
  func resetPadSeen() { queue.async { self.padSeen = false } }
  private(set) var running = false

  static var coresDir: String { Bundle.main.privateFrameworksPath ?? "" }
  /// 롬·저장 폴더. 파일 앱에 보이게 Documents 를 쓴다. 점검할 땐 RETROEMU_DATA 로 바꾼다.
  static var supportDir: URL {
    let u = ProcessInfo.processInfo.environment["RETROEMU_DATA"].map { URL(fileURLWithPath: $0) }
      ?? FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
    try? FileManager.default.createDirectory(at: u, withIntermediateDirectories: true)
    return u
  }

  /// 이 롬에 자동 저장이 있는지(열기 전에 확인)
  static func hasAutoSave(rom: String) -> Bool {
    let base = supportDir.appendingPathComponent("saves").appendingPathComponent((rom as NSString).lastPathComponent).path
    return FileManager.default.fileExists(atPath: base + ".auto.state")
  }

  /// 열기. 실패하면 이유 문자열. paused 면 저장 목록을 먼저 보여주려고 멈춘 채로 시작한다.
  func start(rom: String, system: GameSystem, paused: Bool = false) -> String? {
    var err: String?
    queue.sync {
      let core = Emulator.coresDir + "/" + system.core + ".dylib"
      let sys = Emulator.supportDir.appendingPathComponent("system").path
      let saves = Emulator.supportDir.appendingPathComponent("saves").path
      try? FileManager.default.createDirectory(atPath: sys, withIntermediateDirectories: true)
      try? FileManager.default.createDirectory(atPath: saves, withIntermediateDirectories: true)
      self.system = system
      Emulator.installAssets(system, sysDir: sys)
      #if !targetEnvironment(macCatalyst)
      var gl = GLBridge.provider
      rh_set_gl_provider(&gl)
      #endif
      guard rh_open(core, sys, saves) != 0, rh_load(rom) != 0 else {
        err = String(cString: rh_error())
        rh_close()
        return
      }
      let base = (rom as NSString).lastPathComponent
      romFile = base
      sramPath = saves + "/" + base + ".srm"
      self.base = saves + "/" + base
      _ = rh_sram_load(sramPath)
      lastSerial = 0
      framesRun = 0
      panelPaused = paused
      running = true
      startAudio()
      startTimer()
    }
    return err
  }

  func stop() {
    queue.sync {
      guard running else { return }
      running = false
      timer?.cancel(); timer = nil
      engine?.stop(); engine = nil
      if framesRun >= 300 { saveSlot("auto") }  // 나갈 때 자동 저장(5초 넘게 했을 때만)
      _ = rh_sram_save(sramPath)
      rh_close()
      pushSaves()
    }
  }

  /// 홈 화면으로 가거나 화면이 잠기면 멈추고 자동 저장. 시스템이 앱을 꺼도 진행이 남는다.
  func setBackground(_ bg: Bool) {
    queue.async { [self] in
      guard running, bg != bgPaused else { return }
      bgPaused = bg
      if bg {
        if framesRun >= 300 { saveSlot("auto") }
        _ = rh_sram_save(sramPath)
        engine?.pause()
        pushSaves()
      } else if let e = engine, !e.isRunning {
        try? e.start()
      }
    }
  }

  // MARK: 슬롯 저장

  static let slotIds = ["auto", "1", "2", "3", "4", "5", "6"]

  private func statePath(_ id: String) -> String { base + "." + id + ".state" }
  private func thumbPath(_ id: String) -> String { base + "." + id + ".png" }

  func slots() -> [SaveSlot] {
    let fm = FileManager.default
    return Emulator.slotIds.map { id in
      let date = (try? fm.attributesOfItem(atPath: statePath(id)))?[.modificationDate] as? Date
      return SaveSlot(id: id, date: date, thumb: date == nil ? nil : UIImage(contentsOfFile: thumbPath(id)))
    }
  }

  func setPaused(_ p: Bool) { queue.async { self.panelPaused = p } }

  /// 큐 위에서만 부른다.
  @discardableResult private func saveSlot(_ id: String) -> Bool {
    guard rh_state_save(statePath(id)) != 0 else { return false }
    if let img = currentImage(), let png = UIImage(cgImage: img).pngData() {
      try? png.write(to: URL(fileURLWithPath: thumbPath(id)), options: .atomic)
    } else {
      try? FileManager.default.removeItem(atPath: thumbPath(id))
    }
    return true
  }

  /// 켜고 나서 한 프레임이라도 돌았는지(켜자마자 뜬 목록에서 빈 상태로 덮어쓰기 방지)
  var hasRun: Bool { queue.sync { framesRun > 0 } }

  func save(slot id: String, done: (() -> Void)? = nil) {
    queue.async { [self] in
      guard running else { return }
      guard framesRun > 0 else { toast("게임을 시작한 뒤에 저장할 수 있어요"); return }
      let ok = saveSlot(id)
      toast(ok ? "\(SaveSlot.name(id))에 저장함" : "이 게임은 상태 저장을 못 해요")
      if ok { pushSaves() }
      DispatchQueue.main.async { done?() }
    }
  }

  func load(slot id: String) {
    queue.async { [self] in
      guard running else { return }
      let ok = rh_state_load(statePath(id)) != 0
      if ok { lastSerial = 0 }
      toast(ok ? "\(SaveSlot.name(id)) 불러옴" : "\(SaveSlot.name(id))이 비어 있어요")
    }
  }

  func delete(slot id: String) {
    try? FileManager.default.removeItem(atPath: statePath(id))
    try? FileManager.default.removeItem(atPath: thumbPath(id))
    Sync.deleted(romFile: romFile, names: [romFile + "." + id + ".state", romFile + "." + id + ".png"])
  }

  /// 이 게임 저장을 다른 기기용으로 올린다(뒤에서, 게임은 안 멈춤)
  private func pushSaves() {
    guard Sync.enabled, !romFile.isEmpty else { return }
    let f = romFile
    // 앱이 뒤로 가도 올리기가 끝날 때까지 시간을 달라고 한다
    var task = UIBackgroundTaskIdentifier.invalid
    task = UIApplication.shared.beginBackgroundTask { UIApplication.shared.endBackgroundTask(task) }
    DispatchQueue.global(qos: .utility).async {
      Sync.syncGame(savesDir: Emulator.savesDir, romFile: f, pull: false)
      UIApplication.shared.endBackgroundTask(task)
    }
  }

  private func toast(_ s: String) { DispatchQueue.main.async { self.onToast?(s) } }

  private func startTimer() {
    let fps = rh_fps()
    let t = DispatchSource.makeTimerSource(flags: .strict, queue: queue)
    t.schedule(deadline: .now(), repeating: .nanoseconds(Int(1_000_000_000 / fps)), leeway: .microseconds(100))
    t.setEventHandler { [weak self] in self?.tick() }
    timer = t
    t.resume()
  }

  private func tick() {
    let (pad, fromPad, menu) = padMask()
    if fromPad && !padSeen {
      padSeen = true
      DispatchQueue.main.async { self.onPadUsed?() }
    }
    if menu && !menuHeld { DispatchQueue.main.async { self.onMenu?() } }
    menuHeld = menu
    if panelPaused || bgPaused { return }
    // 아날로그: 터치 스틱, 게임패드 스틱 중 더 기운 쪽
    var lx = Double(touchStick.x), ly = Double(touchStick.y), rx = 0.0, ry = 0.0
    if let gp = GCController.controllers().first?.extendedGamepad {
      let l = gp.leftThumbstick, r = gp.rightThumbstick
      if abs(Double(l.xAxis.value)) + abs(Double(l.yAxis.value)) > abs(lx) + abs(ly) { lx = Double(l.xAxis.value); ly = -Double(l.yAxis.value) }
      rx = Double(r.xAxis.value); ry = -Double(r.yAxis.value)
    }
    rh_set_analog(0, lx, ly)
    rh_set_analog(1, rx, ry)
    rh_set_input(touchMask | pad)
    rh_run()
    framesRun += 1
    sramTick += 1
    if sramTick % 600 == 0 { _ = rh_sram_save(sramPath) }
    let serial = rh_video_serial()
    guard serial != lastSerial else { return }
    lastSerial = serial
    guard let img = currentImage() else { return }
    let aspect = rh_aspect()
    DispatchQueue.main.async { [weak self] in self?.onFrame?(img, aspect) }
  }

  /// 지금 화면(코어가 마지막으로 낸 영상)
  private func currentImage() -> CGImage? {
    var px: UnsafePointer<UInt8>?
    var w: Int32 = 0, h: Int32 = 0
    guard rh_video(&px, &w, &h) != 0, let p = px else { return nil }
    let data = Data(bytes: p, count: Int(w * h * 4))
    guard let prov = CGDataProvider(data: data as CFData) else { return nil }
    return CGImage(width: Int(w), height: Int(h), bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: Int(w) * 4,
                   space: CGColorSpace(name: CGColorSpace.sRGB)!,
                   bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue),
                   provider: prov, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
  }

  /// 게임패드(버튼 위치 기준: 아래=B, 오른쪽=A, 왼쪽=Y, 위=X)와 블루투스 키보드(맥판과 같은 배치).
  /// 두 번째 값은 패드 버튼이 눌렸는지, 세 번째 값은 메뉴를 부르는 조합(셀렉트+스타트, 홈 버튼, Esc)이 눌렸는지.
  private func padMask() -> (UInt32, Bool, Bool) {
    var m: UInt32 = 0
    var menu = false
    func on(_ id: Int) { m |= 1 << UInt32(id) }
    if let pad = GCController.controllers().first?.extendedGamepad {
      func set(_ b: GCControllerButtonInput, _ id: Int) { if b.isPressed { on(id) } }
      set(pad.buttonA, RH_B); set(pad.buttonB, RH_A); set(pad.buttonX, RH_Y); set(pad.buttonY, RH_X)
      set(pad.leftShoulder, RH_L); set(pad.rightShoulder, RH_R)
      set(pad.leftTrigger, RH_L2); set(pad.rightTrigger, RH_R2)
      set(pad.buttonMenu, RH_START)
      if let o = pad.buttonOptions { set(o, RH_SELECT) }
      set(pad.dpad.up, RH_UP); set(pad.dpad.down, RH_DOWN); set(pad.dpad.left, RH_LEFT); set(pad.dpad.right, RH_RIGHT)
      let analog = system.hasAnalog
      let x = analog ? 0 : pad.leftThumbstick.xAxis.value, y = analog ? 0 : pad.leftThumbstick.yAxis.value
      if x < -0.5 { on(RH_LEFT) }
      if x > 0.5 { on(RH_RIGHT) }
      if y > 0.5 { on(RH_UP) }
      if y < -0.5 { on(RH_DOWN) }
      if pad.buttonHome?.isPressed == true { menu = true }
    }
    let fromPad = m != 0
    if let kb = GCKeyboard.coalesced?.keyboardInput {
      let keys: [(GCKeyCode, Int)] = [
        (.upArrow, RH_UP), (.downArrow, RH_DOWN), (.leftArrow, RH_LEFT), (.rightArrow, RH_RIGHT),
        (.keyZ, RH_B), (.keyX, RH_A), (.keyA, RH_Y), (.keyS, RH_X),
        (.keyQ, RH_L), (.keyW, RH_R), (.keyE, RH_L2), (.keyR, RH_R2),
        (.returnOrEnter, RH_START), (.deleteOrBackspace, RH_SELECT), (.tab, RH_SELECT),
      ]
      for (k, id) in keys where kb.button(forKeyCode: k)?.isPressed == true { on(id) }
      if kb.button(forKeyCode: .escape)?.isPressed == true { menu = true }
    }
    let ss: UInt32 = (1 << UInt32(RH_SELECT)) | (1 << UInt32(RH_START))
    if m & ss == ss { menu = true; m &= ~ss }
    return (m, fromPad, menu)
  }

  /// DS 아래 화면 터치. x·y 는 게임 그림 기준 0~1
  func setPointer(_ x: Double, _ y: Double, _ down: Bool) { queue.async { rh_set_pointer(x, y, down ? 1 : 0) } }

  /// PSP·게임큐브 코어 자료를 처음 한 번 앱 번들(sysassets)에서 시스템 폴더로 복사한다
  static func installAssets(_ system: GameSystem, sysDir: String) {
    guard let name = system.sysAssets, let res = Bundle.main.resourcePath else { return }
    let dst = sysDir + "/" + name, src = res + "/sysassets/" + name
    let fm = FileManager.default
    if fm.fileExists(atPath: dst) || !fm.fileExists(atPath: src) { return }
    try? fm.copyItem(atPath: src, toPath: dst)
  }

  private func startAudio() {
    try? AVAudioSession.sharedInstance().setCategory(.playback, mode: .default)
    try? AVAudioSession.sharedInstance().setActive(true)
    let rate = rh_sample_rate()
    let eng = AVAudioEngine()
    let fmt = AVAudioFormat(commonFormat: .pcmFormatInt16, sampleRate: rate, channels: 2, interleaved: true)!
    let src = AVAudioSourceNode(format: fmt) { _, _, frames, abl in
      let buf = UnsafeMutableAudioBufferListPointer(abl)
      if let p = buf[0].mData?.assumingMemoryBound(to: Int16.self) {
        _ = rh_audio_read(p, Int32(frames))
      }
      return noErr
    }
    eng.attach(src)
    eng.connect(src, to: eng.mainMixerNode, format: fmt)
    do { try eng.start(); engine = eng } catch { NSLog("audio fail: \(error)") }
  }
}

struct SaveSlot: Identifiable {
  let id: String
  let date: Date?
  let thumb: UIImage?
  var isAuto: Bool { SaveSlot.isAutoId(id) }
  static func isAutoId(_ id: String) -> Bool { id == "auto" }
  static func name(_ id: String) -> String { id == "auto" ? "자동 저장" : "\(id)번 슬롯" }
}

#if !targetEnvironment(macCatalyst)
import OpenGLES

/// PSP·N64·게임큐브·PS2 코어용 OpenGL ES 컨텍스트(EAGL). rhost 가 코어를 부를 때마다 붙였다 뗀다.
enum GLBridge {
  static var ctx: EAGLContext?
  static let provider = rh_gl_provider(
    create: { gles, _, _ in
      guard gles != 0 else { return 0 }
      // GLES2 를 달라는 코어(PPSSPP)도 GLES3 컨텍스트에서 그대로 돈다. 3 이 안 되면 2.
      GLBridge.ctx = EAGLContext(api: .openGLES3) ?? EAGLContext(api: .openGLES2)
      return GLBridge.ctx == nil ? 0 : 1
    },
    make_current: { on in EAGLContext.setCurrent(on != 0 ? GLBridge.ctx : nil) },
    get_proc: { name in
      guard let name else { return nil }
      return dlsym(UnsafeMutableRawPointer(bitPattern: -2), name)   // RTLD_DEFAULT
    },
    destroy: { EAGLContext.setCurrent(nil); GLBridge.ctx = nil })
}
#endif
