import AppKit
import AVFoundation
import CRhost
import GameController

/// 코어 한 개를 돌리는 쪽. 모든 코어 호출은 직렬 큐 하나에서만 한다.
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
  /// 저장 목록을 보는 동안 멈춘다(큐에서만 읽고 씀)
  private var paused = false
  /// 이번에 켜고 나서 돈 프레임 수. 거의 안 했으면 나갈 때 자동 저장을 덮어쓰지 않는다.
  private var framesRun = 0

  /// 키보드 입력 비트마스크(메인 스레드에서 쓰고 큐에서 읽음)
  var keyMask: UInt32 = 0
  /// 오른쪽 스틱용 키(I/J/K/L) 상태: 위·아래·왼·오 비트 0~3
  var rightKeys: UInt32 = 0
  private var system: GameSystem = .nes
  /// 새 영상이 나오면 호출(메인 스레드)
  var onFrame: ((CGImage, Double) -> Void)?
  var onToast: ((String) -> Void)?
  private(set) var running = false

  static var coresDir: String {
    Bundle.main.resourcePath.map { $0 + "/cores" } ?? ""
  }
  static var supportDir: URL {
    let u = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0].appendingPathComponent("RetroEmu")
    try? FileManager.default.createDirectory(at: u, withIntermediateDirectories: true)
    return u
  }

  /// 열기. 실패하면 이유 문자열.
  func start(rom: String, system: GameSystem) -> String? {
    var err: String?
    queue.sync {
      let core = Emulator.coresDir + "/" + system.core + ".dylib"
      let sys = Emulator.supportDir.appendingPathComponent("system").path
      let saves = Emulator.supportDir.appendingPathComponent("saves").path
      try? FileManager.default.createDirectory(atPath: sys, withIntermediateDirectories: true)
      try? FileManager.default.createDirectory(atPath: saves, withIntermediateDirectories: true)
      self.system = system
      Emulator.installAssets(system, sysDir: sys)
      guard rh_open(core, sys, saves) != 0, rh_load(rom) != 0 else {
        err = String(cString: rh_error())
        rh_close()
        return
      }
      let base = (rom as NSString).lastPathComponent
      romFile = base
      sramPath = saves + "/" + base + ".srm"
      self.base = saves + "/" + base
      migrateOldState()
      _ = rh_sram_load(sramPath)
      lastSerial = 0
      framesRun = 0
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

  // MARK: 슬롯 저장

  static let slotIds = ["auto", "1", "2", "3", "4", "5", "6"]

  private func statePath(_ id: String) -> String { base + "." + id + ".state" }
  private func thumbPath(_ id: String) -> String { base + "." + id + ".png" }

  /// 예전 한 칸짜리 저장(<롬>.state)은 1번 슬롯으로 옮긴다.
  private func migrateOldState() {
    let fm = FileManager.default, old = base + ".state"
    if fm.fileExists(atPath: old), !fm.fileExists(atPath: statePath("1")) { try? fm.moveItem(atPath: old, toPath: statePath("1")) }
  }

  func slots() -> [SaveSlot] {
    let fm = FileManager.default
    return Emulator.slotIds.map { id in
      let date = (try? fm.attributesOfItem(atPath: statePath(id)))?[.modificationDate] as? Date
      return SaveSlot(id: id, date: date, thumb: date == nil ? nil : NSImage(contentsOfFile: thumbPath(id)))
    }
  }

  func setPaused(_ p: Bool) { queue.async { self.paused = p } }

  /// 큐 위에서만 부른다.
  @discardableResult private func saveSlot(_ id: String) -> Bool {
    guard rh_state_save(statePath(id)) != 0 else { return false }
    if let img = currentImage(), let png = NSBitmapImageRep(cgImage: img).representation(using: .png, properties: [:]) {
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
    DispatchQueue.global(qos: .utility).async { Sync.syncGame(savesDir: Emulator.savesDir, romFile: f, pull: false) }
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
    if paused { return }
    var keys = keyMask
    // 키보드: 스틱이 주된 기종은 방향키를 왼쪽 스틱으로, I/J/K/L 은 오른쪽 스틱(N64 는 C 버튼)
    var lx = 0.0, ly = 0.0
    if system.stickFirst {
      let dirs: UInt32 = (1 << UInt32(RH_UP)) | (1 << UInt32(RH_DOWN)) | (1 << UInt32(RH_LEFT)) | (1 << UInt32(RH_RIGHT))
      if keys & (1 << UInt32(RH_LEFT)) != 0 { lx -= 1 }
      if keys & (1 << UInt32(RH_RIGHT)) != 0 { lx += 1 }
      if keys & (1 << UInt32(RH_UP)) != 0 { ly -= 1 }
      if keys & (1 << UInt32(RH_DOWN)) != 0 { ly += 1 }
      keys &= ~dirs
    }
    var rx = 0.0, ry = 0.0
    if rightKeys & 1 != 0 { ry -= 1 }
    if rightKeys & 2 != 0 { ry += 1 }
    if rightKeys & 4 != 0 { rx -= 1 }
    if rightKeys & 8 != 0 { rx += 1 }
    // 게임패드 스틱이 더 크게 기울었으면 그걸 쓴다
    if let pad = GCController.controllers().first?.extendedGamepad {
      let pl = pad.leftThumbstick, pr = pad.rightThumbstick
      if abs(Double(pl.xAxis.value)) + abs(Double(pl.yAxis.value)) > abs(lx) + abs(ly) { lx = Double(pl.xAxis.value); ly = -Double(pl.yAxis.value) }
      if abs(Double(pr.xAxis.value)) + abs(Double(pr.yAxis.value)) > abs(rx) + abs(ry) { rx = Double(pr.xAxis.value); ry = -Double(pr.yAxis.value) }
    }
    rh_set_analog(0, lx, ly)
    rh_set_analog(1, rx, ry)
    rh_set_input(keys | padMask(stick: !system.hasAnalog))
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

  /// 게임패드: 버튼 위치 기준으로 (아래=B, 오른쪽=A, 왼쪽=Y, 위=X)
  /// DS 아래 화면 터치(마우스). x·y 는 게임 그림 기준 0~1
  func setPointer(_ x: Double, _ y: Double, _ down: Bool) { queue.async { rh_set_pointer(x, y, down ? 1 : 0) } }

  /// PSP·게임큐브 코어가 쓰는 자료를 처음 한 번 시스템 폴더에 복사한다(앱 Resources/sysassets)
  static func installAssets(_ system: GameSystem, sysDir: String) {
    guard let name = system.sysAssets, let res = Bundle.main.resourcePath else { return }
    let dst = sysDir + "/" + name, src = res + "/sysassets/" + name
    let fm = FileManager.default
    if fm.fileExists(atPath: dst) || !fm.fileExists(atPath: src) { return }
    try? fm.copyItem(atPath: src, toPath: dst)
  }

  private func padMask(stick: Bool = true) -> UInt32 {
    guard let pad = GCController.controllers().first?.extendedGamepad else { return 0 }
    var m: UInt32 = 0
    func set(_ b: GCControllerButtonInput, _ id: Int) { if b.isPressed { m |= 1 << UInt32(id) } }
    set(pad.buttonA, RH_B); set(pad.buttonB, RH_A); set(pad.buttonX, RH_Y); set(pad.buttonY, RH_X)
    set(pad.leftShoulder, RH_L); set(pad.rightShoulder, RH_R)
    set(pad.leftTrigger, RH_L2); set(pad.rightTrigger, RH_R2)
    set(pad.buttonMenu, RH_START)
    if let o = pad.buttonOptions { set(o, RH_SELECT) }
    set(pad.dpad.up, RH_UP); set(pad.dpad.down, RH_DOWN); set(pad.dpad.left, RH_LEFT); set(pad.dpad.right, RH_RIGHT)
    guard stick else { return m }
    let x = pad.leftThumbstick.xAxis.value, y = pad.leftThumbstick.yAxis.value
    if x < -0.5 { m |= 1 << UInt32(RH_LEFT) }
    if x > 0.5 { m |= 1 << UInt32(RH_RIGHT) }
    if y > 0.5 { m |= 1 << UInt32(RH_UP) }
    if y < -0.5 { m |= 1 << UInt32(RH_DOWN) }
    return m
  }

  private func startAudio() {
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

  /// I/J/K/L -> 오른쪽 스틱 비트(위·아래·왼·오)
  static func rightStickBit(forKeyCode k: UInt16) -> UInt32? {
    switch k { case 34: return 1; case 40: return 2; case 38: return 4; case 37: return 8; default: return nil }
  }

  /// 키보드 키코드 -> 조이패드 비트
  static func bit(forKeyCode k: UInt16) -> UInt32? {
    let id: Int
    switch k {
    case 126: id = RH_UP
    case 125: id = RH_DOWN
    case 123: id = RH_LEFT
    case 124: id = RH_RIGHT
    case 6: id = RH_B        // Z
    case 7: id = RH_A        // X
    case 0: id = RH_Y        // A
    case 1: id = RH_X        // S
    case 12: id = RH_L       // Q
    case 13: id = RH_R       // W
    case 14: id = RH_L2      // E
    case 15: id = RH_R2      // R
    case 36: id = RH_START   // Return
    case 51, 48: id = RH_SELECT // Delete, Tab
    default: return nil
    }
    return 1 << UInt32(id)
  }
}

struct SaveSlot: Identifiable {
  let id: String
  let date: Date?
  let thumb: NSImage?
  var isAuto: Bool { SaveSlot.isAutoId(id) }
  static func isAutoId(_ id: String) -> Bool { id == "auto" }
  static func name(_ id: String) -> String { id == "auto" ? "자동 저장" : "\(id)번 슬롯" }
}
