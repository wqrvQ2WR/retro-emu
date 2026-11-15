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
  private var statePath = ""
  private var sramTick = 0

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
      statePath = saves + "/" + base + ".state"
      _ = rh_sram_load(sramPath)
      lastSerial = 0
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
      _ = rh_sram_save(sramPath)
      rh_close()
    }
  }

  func saveState() {
    queue.async { [self] in
      guard running else { return }
      let ok = rh_state_save(statePath) != 0
      toast(ok ? "상태 저장함" : "이 게임은 상태 저장을 못 해요")
    }
  }

  func loadState() {
    queue.async { [self] in
      guard running else { return }
      let ok = rh_state_load(statePath) != 0
      toast(ok ? "상태 불러옴" : "저장한 상태가 없어요")
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
    rh_set_input(keyMask | padMask())
    rh_run()
    sramTick += 1
    if sramTick % 600 == 0 { _ = rh_sram_save(sramPath) }
    let serial = rh_video_serial()
    guard serial != lastSerial else { return }
    lastSerial = serial
    var px: UnsafePointer<UInt8>?
    var w: Int32 = 0, h: Int32 = 0
    guard rh_video(&px, &w, &h) != 0, let p = px else { return }
    let data = Data(bytes: p, count: Int(w * h * 4))
    guard let prov = CGDataProvider(data: data as CFData),
          let img = CGImage(width: Int(w), height: Int(h), bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: Int(w) * 4,
                            space: CGColorSpace(name: CGColorSpace.sRGB)!,
                            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue),
                            provider: prov, decode: nil, shouldInterpolate: false, intent: .defaultIntent) else { return }
    let aspect = rh_aspect()
    DispatchQueue.main.async { [weak self] in self?.onFrame?(img, aspect) }
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
