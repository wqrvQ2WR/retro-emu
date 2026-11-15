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
  /// 저장 목록을 보는 동안 멈춘다(큐에서만 읽고 씀)
  private var paused = false
  /// 이번에 켜고 나서 돈 프레임 수. 거의 안 했으면 나갈 때 자동 저장을 덮어쓰지 않는다.
  private var framesRun = 0

  /// 키보드 입력 비트마스크(메인 스레드에서 쓰고 큐에서 읽음)
  var keyMask: UInt32 = 0
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
      guard rh_open(core, sys, saves) != 0, rh_load(rom) != 0 else {
        err = String(cString: rh_error())
        rh_close()
        return
      }
      let base = (rom as NSString).lastPathComponent
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

  func save(slot id: String, done: (() -> Void)? = nil) {
    queue.async { [self] in
      guard running else { return }
      let ok = saveSlot(id)
      toast(ok ? "\(SaveSlot.name(id))에 저장함" : "이 게임은 상태 저장을 못 해요")
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
    rh_set_input(keyMask | padMask())
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
  private func padMask() -> UInt32 {
    guard let pad = GCController.controllers().first?.extendedGamepad else { return 0 }
    var m: UInt32 = 0
    func set(_ b: GCControllerButtonInput, _ id: Int) { if b.isPressed { m |= 1 << UInt32(id) } }
    set(pad.buttonA, RH_B); set(pad.buttonB, RH_A); set(pad.buttonX, RH_Y); set(pad.buttonY, RH_X)
    set(pad.leftShoulder, RH_L); set(pad.rightShoulder, RH_R)
    set(pad.leftTrigger, RH_L2); set(pad.rightTrigger, RH_R2)
    set(pad.buttonMenu, RH_START)
    if let o = pad.buttonOptions { set(o, RH_SELECT) }
    set(pad.dpad.up, RH_UP); set(pad.dpad.down, RH_DOWN); set(pad.dpad.left, RH_LEFT); set(pad.dpad.right, RH_RIGHT)
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
