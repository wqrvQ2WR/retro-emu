import GameController
import SwiftUI
import UIKit

// MARK: - 게임 화면 그리기

/// 코어 영상을 레이어 contents 로 바로 바꿔 끼운다(SwiftUI 상태를 안 거쳐서 빠름). 픽셀은 nearest 로 키운다.
final class ScreenView: UIView {
  private let image = CALayer()
  /// DS 아래 화면 터치(게임 그림 기준 0~1). 비어 있으면 터치를 안 받는다.
  var onPointer: ((Double, Double, Bool) -> Void)? { didSet { isMultipleTouchEnabled = false } }
  private func pointer(_ t: Set<UITouch>, _ down: Bool) {
    guard let onPointer, let p = t.first?.location(in: self) else { return }
    let f = image.frame
    guard f.width > 0 else { return }
    let u = (p.x - f.minX) / f.width, v = (p.y - f.minY) / f.height
    // 돌린 화면에서 누른 자리를 원래 그림 자리로
    let (x, y): (Double, Double) = rotation == 1 ? (v, 1 - u) : rotation == 2 ? (1 - u, 1 - v) : rotation == 3 ? (1 - v, u) : (u, v)
    onPointer(x, y, down && x >= 0 && x <= 1 && y >= 0 && y <= 1)
  }
  override func touchesBegan(_ t: Set<UITouch>, with e: UIEvent?) { pointer(t, true) }
  override func touchesMoved(_ t: Set<UITouch>, with e: UIEvent?) { pointer(t, true) }
  override func touchesEnded(_ t: Set<UITouch>, with e: UIEvent?) { pointer(t, false) }
  override func touchesCancelled(_ t: Set<UITouch>, with e: UIEvent?) { pointer(t, false) }
  private var aspect: Double = 4.0 / 3.0
  /// 화면 돌리기(시계 방향 90도 단위). 옆으로 드는 DS 게임(러브플러스)용.
  var rotation = 0 { didSet { if rotation != oldValue { setNeedsLayout() } } }
  /// 세로 화면에선 위에 붙인다
  var alignTop = false { didSet { setNeedsLayout() } }

  override init(frame: CGRect) {
    super.init(frame: frame)
    backgroundColor = .clear
    image.magnificationFilter = .nearest
    image.minificationFilter = .linear
    image.actions = ["contents": NSNull(), "bounds": NSNull(), "position": NSNull()]
    layer.addSublayer(image)
  }
  required init?(coder: NSCoder) { fatalError() }

  func show(_ img: CGImage, aspect a: Double) {
    image.contents = img
    let a = a > 0 ? a : Double(img.width) / Double(img.height)
    if abs(a - aspect) > 0.001 { aspect = a; setNeedsLayout() }
  }

  override func layoutSubviews() {
    super.layoutSubviews()
    let b = bounds.size
    guard b.width > 0, b.height > 0 else { return }
    let a = rotation % 2 == 1 ? 1 / aspect : aspect
    var w = b.width, h = w / a
    if h > b.height { h = b.height; w = h * a }
    let s = window?.screen.scale ?? 2
    w = (w * s).rounded(.down) / s; h = (h * s).rounded(.down) / s
    let f = CGRect(x: ((b.width - w) / 2 * s).rounded() / s, y: alignTop ? 0 : ((b.height - h) / 2 * s).rounded() / s, width: w, height: h)
    // 돌리기 전 크기로 놓고 가운데를 축으로 돌린다
    image.transform = CATransform3DIdentity
    image.bounds = CGRect(x: 0, y: 0, width: rotation % 2 == 1 ? h : w, height: rotation % 2 == 1 ? w : h)
    image.position = CGPoint(x: f.midX, y: f.midY)
    image.transform = CATransform3DMakeRotation(CGFloat(rotation) * .pi / 2, 0, 0, 1)
  }
}

struct ScreenRep: UIViewRepresentable {
  let emu: Emulator
  var alignTop = false
  var touch = false
  var rotation = 0
  func makeUIView(context: Context) -> ScreenView {
    let v = ScreenView()
    if touch { v.onPointer = { x, y, d in emu.setPointer(x, y, d) } }
    emu.onFrame = { [weak v] img, a in v?.show(img, aspect: a) }
    return v
  }
  func updateUIView(_ v: ScreenView, context: Context) {
    if v.alignTop != alignTop { v.alignTop = alignTop }
    v.rotation = rotation
  }
}

// MARK: - 터치 버튼

/// 버튼 전체를 UIView 하나가 맡는다. 손가락이 버튼 사이를 미끄러져도 따라가고, 여러 손가락을 같이 쓴다.
final class ControlsView: UIView {
  struct Button {
    var bit: Int
    var label: String
    var rect: CGRect
    var round: Bool
  }

  var system: GameSystem = .nes { didSet { setNeedsLayout() } }
  var big = false { didSet { setNeedsLayout() } }
  var stick = false { didSet { setNeedsLayout() } }
  /// 가로 화면이면 게임 화면 위에 반투명하게 겹친다
  var overlay = false { didSet { setNeedsLayout() } }
  var onMask: ((UInt32) -> Void)?
  /// 아날로그 기종이면 왼쪽 패드를 아날로그 스틱으로(-1~1)
  var onStick: ((CGPoint) -> Void)?
  /// N64 C 버튼 -> 오른쪽 스틱
  var onRStick: ((CGPoint) -> Void)?
  /// C 버튼은 비트 24~27(위·아래·왼쪽·오른쪽)로 들고 있다가 오른쪽 스틱으로 바꾼다
  static let cBase = 24

  private var buttons: [Button] = []
  private var padCenter = CGPoint.zero
  private var padRadius: CGFloat = 60
  /// 십자키(조이스틱)를 잡은 손가락과 그 위치
  private var padTouch: UITouch?
  private var padPoint: CGPoint?
  private var held: UInt32 = 0
  private let haptic = UIImpactFeedbackGenerator(style: .light)

  override init(frame: CGRect) {
    super.init(frame: frame)
    isMultipleTouchEnabled = true
    isOpaque = false
    backgroundColor = .clear
    contentMode = .redraw
  }
  required init?(coder: NSCoder) { fatalError() }

  // MARK: 배치

  override func layoutSubviews() {
    super.layoutSubviews()
    let W = bounds.width, H = bounds.height
    guard W > 0, H > 0 else { return }
    let k: CGFloat = big ? 1.23 : 1.0
    let u = overlay ? min(H / 360, W / 760) : min(W / 390, H / 330)
    buttons = []

    // 어깨 버튼(맨 위 양 끝)
    let sh = 38 * u, sw = 88 * u * min(k, 1.12), top = 8 * u
    if system == .n64 || system == .gc {
      // 버튼 이름은 코어가 알려 주는 대로. N64: L2=Z, L, R(R2 는 C 버튼 모드라 뺌). 게임큐브: L2=L, R=Z, R2=R(L 은 Triforce 시험용이라 뺌)
      let left: [(Int, String)] = system == .n64 ? [(RH_L2, "Z"), (RH_L, "L")] : [(RH_L2, "L")]
      let right: [(Int, String)] = system == .n64 ? [(RH_R, "R")] : [(RH_R2, "R"), (RH_R, "Z")]
      for (i, (bit, l)) in left.enumerated() {
        let w = i == 0 ? sw : sw * 0.75, x = i == 0 ? 12 * u : 20 * u + sw
        buttons.append(Button(bit: bit, label: l, rect: CGRect(x: x, y: top, width: w, height: sh), round: false))
      }
      for (i, (bit, l)) in right.enumerated() {
        let w = i == 0 ? sw : sw * 0.75, x = i == 0 ? W - 12 * u - sw : W - 20 * u - sw * 1.75
        buttons.append(Button(bit: bit, label: l, rect: CGRect(x: x, y: top, width: w, height: sh), round: false))
      }
    } else if system.hasShoulders {
      buttons.append(Button(bit: RH_L, label: "L", rect: CGRect(x: 12 * u, y: top, width: sw, height: sh), round: false))
      buttons.append(Button(bit: RH_R, label: "R", rect: CGRect(x: W - 12 * u - sw, y: top, width: sw, height: sh), round: false))
      if system.hasTriggers {
        buttons.append(Button(bit: RH_L2, label: "L2", rect: CGRect(x: 20 * u + sw, y: top, width: sw * 0.75, height: sh), round: false))
        buttons.append(Button(bit: RH_R2, label: "R2", rect: CGRect(x: W - 20 * u - sw * 1.75, y: top, width: sw * 0.75, height: sh), round: false))
      }
    }

    // 십자키와 A/B 버튼
    padRadius = min(70 * u * k, W * (overlay ? 0.15 : 0.23), H * 0.36)
    let midY = overlay ? H * 0.55 : (system.hasShoulders ? H * 0.50 : H * 0.44)
    let side = max(padRadius + 16 * u, W * (overlay ? 0.12 : 0.25))
    padCenter = CGPoint(x: side, y: midY)
    let face = CGPoint(x: W - side, y: midY)
    if system == .n64 {
      // N64 코어: 패드 B=A, Y=B. 노란 C 버튼 넷은 오른쪽 스틱으로.
      let r = min(30 * u * k, padRadius * 0.44)
      buttons.append(Button(bit: RH_B, label: "A", rect: CGRect(x: face.x - r * 0.3 - r, y: face.y + r * 1.0 - r, width: r * 2, height: r * 2), round: true))
      buttons.append(Button(bit: RH_Y, label: "B", rect: CGRect(x: face.x - r * 1.9 - r * 0.85, y: face.y - r * 0.2 - r * 0.85, width: r * 1.7, height: r * 1.7), round: true))
      let cr = r * 0.6, cc = CGPoint(x: face.x + r * 1.1, y: face.y - r * 1.1), cd = cr * 1.55
      for (i, (dx, dy)) in [(0.0, -1.0), (0.0, 1.0), (-1.0, 0.0), (1.0, 0.0)].enumerated() {
        buttons.append(Button(bit: ControlsView.cBase + i, label: "@c\(i)",
                              rect: CGRect(x: cc.x + CGFloat(dx) * cd - cr, y: cc.y + CGFloat(dy) * cd - cr, width: cr * 2, height: cr * 2), round: true))
      }
    } else if system.hasXY {
      let r = min(29 * u * k, padRadius * 0.42), d = r * 1.72
      let ps1 = system == .ps1 || system == .ps2 || system == .psp
      let set: [(Int, String, CGFloat, CGFloat)] = [
        (RH_X, ps1 ? "@ps-tri" : "X", 0, -d), (RH_Y, ps1 ? "@ps-sq" : "Y", -d, 0),
        (RH_A, ps1 ? "@ps-cir" : "A", d, 0), (RH_B, ps1 ? "@ps-cross" : "B", 0, d),
      ]
      for (bit, l, dx, dy) in set {
        buttons.append(Button(bit: bit, label: l, rect: CGRect(x: face.x + dx - r, y: face.y + dy - r, width: r * 2, height: r * 2), round: true))
      }
    } else {
      let r = min(34 * u * k, padRadius * 0.5)
      buttons.append(Button(bit: RH_B, label: "B", rect: CGRect(x: face.x - r * 1.15 - r, y: face.y + r * 0.6 - r, width: r * 2, height: r * 2), round: true))
      buttons.append(Button(bit: RH_A, label: "A", rect: CGRect(x: face.x + r * 1.15 - r, y: face.y - r * 0.6 - r, width: r * 2, height: r * 2), round: true))
    }

    // 셀렉트·스타트(아래 가운데)
    let pw = 66 * u * min(k, 1.1), ph = 28 * u * min(k, 1.1)
    let py = H - ph - (overlay ? 10 * u : 14 * u)
    // 가로 화면에선 게임 화면을 안 가리게 십자키·A/B 아래로
    let selX = overlay ? padCenter.x - pw / 2 : W / 2 - pw - 8 * u
    let staX = overlay ? face.x - pw / 2 : W / 2 + 8 * u
    // N64·게임큐브엔 셀렉트가 없다(돌핀은 그 자리가 Triforce 코인)
    let noSelect = system == .n64 || system == .gc
    if !noSelect { buttons.append(Button(bit: RH_SELECT, label: "셀렉트", rect: CGRect(x: selX, y: py, width: pw, height: ph), round: false)) }
    let stX = noSelect && !overlay ? W / 2 - pw / 2 : staX
    buttons.append(Button(bit: RH_START, label: "스타트", rect: CGRect(x: stX, y: py, width: pw, height: ph), round: false))
    if system == .nds {
      // DS 마이크: melonDS 의 "Make microphone noise"(패드 L2). 누르고 있는 동안 소리가 들어간다.
      let m = ph * 1.2, mx = overlay ? staX + pw + 10 * u : staX + pw + 12 * u
      buttons.append(Button(bit: RH_L2, label: "@mic", rect: CGRect(x: mx, y: py + ph / 2 - m / 2, width: m, height: m), round: true))
    }
    setNeedsDisplay()
  }

  // MARK: 입력

  override func touchesBegan(_ t: Set<UITouch>, with e: UIEvent?) {
    for touch in t where padTouch == nil {
      let p = touch.location(in: self)
      if hypot(p.x - padCenter.x, p.y - padCenter.y) < padRadius * 1.25 { padTouch = touch; padPoint = p }
    }
    update(e)
  }
  override func touchesMoved(_ t: Set<UITouch>, with e: UIEvent?) {
    if let pt = padTouch, t.contains(pt) { padPoint = pt.location(in: self) }
    update(e)
  }
  override func touchesEnded(_ t: Set<UITouch>, with e: UIEvent?) { end(t, e) }
  override func touchesCancelled(_ t: Set<UITouch>, with e: UIEvent?) { end(t, e) }

  private func end(_ t: Set<UITouch>, _ e: UIEvent?) {
    if let pt = padTouch, t.contains(pt) { padTouch = nil; padPoint = nil }
    update(e, ended: t)
  }

  /// 지금 닿아 있는 손가락 전부로 비트마스크를 다시 만든다
  private func update(_ e: UIEvent?, ended: Set<UITouch> = []) {
    var m: UInt32 = 0
    if system.hasAnalog {
      var v = CGPoint.zero
      if let p = padPoint {
        let lim = padRadius * 0.8
        v = CGPoint(x: max(-1, min(1, (p.x - padCenter.x) / lim)), y: max(-1, min(1, (p.y - padCenter.y) / lim)))
      }
      onStick?(v)
    } else if let p = padPoint { m |= padBits(p) }
    for touch in e?.touches(for: self) ?? [] where !ended.contains(touch) && touch !== padTouch {
      let p = touch.location(in: self)
      for b in buttons where b.rect.insetBy(dx: -10, dy: -10).contains(p) { m |= 1 << UInt32(b.bit) }
    }
    if m != held {
      if m & ~held != 0 { haptic.impactOccurred(intensity: 0.6) }
      held = m
      onMask?(m & 0xFFFF)
      let c = Int(m >> UInt32(ControlsView.cBase)) & 0xF
      onRStick?(CGPoint(x: (c & 8 != 0 ? 1 : 0) - (c & 4 != 0 ? 1 : 0), y: (c & 2 != 0 ? 1 : 0) - (c & 1 != 0 ? 1 : 0)))
      setNeedsDisplay()
    } else if stick || system.hasAnalog {
      setNeedsDisplay()  // 조이스틱 손잡이 위치
    }
  }

  /// 8방향. 가운데 조금은 안 눌린 걸로 친다.
  private func padBits(_ p: CGPoint) -> UInt32 {
    let dx = p.x - padCenter.x, dy = p.y - padCenter.y
    guard hypot(dx, dy) > padRadius * 0.22 else { return 0 }
    let a = atan2(-dy, dx)  // 오른쪽 0, 위 +
    let full = CGFloat.pi * 2
    let turn: CGFloat = (a + full).truncatingRemainder(dividingBy: full) + CGFloat.pi / 8
    let sector = Int(turn / (CGFloat.pi / 4)) % 8
    let dirs: [[Int]] = [[RH_RIGHT], [RH_RIGHT, RH_UP], [RH_UP], [RH_UP, RH_LEFT], [RH_LEFT], [RH_LEFT, RH_DOWN], [RH_DOWN], [RH_DOWN, RH_RIGHT]]
    return dirs[sector].reduce(0) { $0 | 1 << UInt32($1) }
  }

  // MARK: 그리기

  private func pressed(_ bit: Int) -> Bool { held & (1 << UInt32(bit)) != 0 }

  /// 안드로이드판과 같은 반투명 흰 버튼. 눌리면 진해진다.
  override func draw(_ rect: CGRect) {
    let dim: CGFloat = overlay ? 0.8 : 1
    func fill(_ path: UIBezierPath, _ on: Bool) {
      UIColor.white.withAlphaComponent((on ? 0.6 : 0.3) * dim).setFill()
      path.fill()
    }
    func label(_ s: String, in r: CGRect, size: CGFloat) {
      let a: [NSAttributedString.Key: Any] = [.font: UIFont.systemFont(ofSize: size, weight: .heavy),
                                               .foregroundColor: UIColor.white.withAlphaComponent(0.95 * dim)]
      let t = NSAttributedString(string: s, attributes: a), sz = t.size()
      t.draw(at: CGPoint(x: r.midX - sz.width / 2, y: r.midY - sz.height / 2))
    }

    // 십자키 / 조이스틱: 바탕 원 위에
    let R = padRadius, pc = padCenter
    UIColor.white.withAlphaComponent(0.16 * dim).setFill()
    UIBezierPath(ovalIn: CGRect(x: pc.x - R, y: pc.y - R, width: R * 2, height: R * 2)).fill()
    if stick || system.hasAnalog {
      var kp = pc
      if let p = padPoint {
        let dx = p.x - pc.x, dy = p.y - pc.y, d = hypot(dx, dy), lim = R * 0.55
        kp = d > lim ? CGPoint(x: pc.x + dx / d * lim, y: pc.y + dy / d * lim) : p
      }
      let kr = R * 0.44
      fill(UIBezierPath(ovalIn: CGRect(x: kp.x - kr, y: kp.y - kr, width: kr * 2, height: kr * 2)), padPoint != nil)
    } else {
      // 네 방향 키(가운데는 비움)
      let k = R * 0.42, gap = R * 0.06, len = R * 0.9 - k / 2
      let keys: [(Int, CGRect)] = [
        (RH_UP, CGRect(x: pc.x - k / 2, y: pc.y - R * 0.9, width: k, height: len - gap)),
        (RH_DOWN, CGRect(x: pc.x - k / 2, y: pc.y + k / 2 + gap, width: k, height: len - gap)),
        (RH_LEFT, CGRect(x: pc.x - R * 0.9, y: pc.y - k / 2, width: len - gap, height: k)),
        (RH_RIGHT, CGRect(x: pc.x + k / 2 + gap, y: pc.y - k / 2, width: len - gap, height: k)),
      ]
      for (bit, r) in keys { fill(UIBezierPath(roundedRect: r, cornerRadius: k * 0.3), pressed(bit)) }
    }

    for b in buttons {
      let path = b.round ? UIBezierPath(ovalIn: b.rect) : UIBezierPath(roundedRect: b.rect, cornerRadius: b.rect.height / 2)
      fill(path, pressed(b.bit))
      if b.label.hasPrefix("@ps") { psShape(b.label, in: b.rect, on: pressed(b.bit), dim: dim); continue }
      if b.label.hasPrefix("@c") { cArrow(Int(b.label.dropFirst(2)) ?? 0, in: b.rect, on: pressed(b.bit), dim: dim); continue }
      if b.label == "@mic" { micIcon(in: b.rect, dim: dim); continue }
      let size = b.round ? b.rect.height * (b.label.count > 1 ? 0.26 : 0.36) : b.rect.height * 0.42
      label(b.label, in: b.rect, size: size)
    }
  }
}

/// N64 C 버튼: 노란 삼각형 화살표(0 위, 1 아래, 2 왼쪽, 3 오른쪽)
private func cArrow(_ dir: Int, in r: CGRect, on: Bool, dim: CGFloat) {
  let w = r.width * 0.4, c = CGPoint(x: r.midX, y: r.midY)
  let pts: [CGPoint] = [CGPoint(x: 0, y: -0.5), CGPoint(x: 0.5, y: 0.4), CGPoint(x: -0.5, y: 0.4)]
  let ang: CGFloat = [0, .pi, -.pi / 2, .pi / 2][dir]
  let p = UIBezierPath()
  for (i, q) in pts.enumerated() {
    let x = q.x * cos(ang) - q.y * sin(ang), y = q.x * sin(ang) + q.y * cos(ang)
    let pt = CGPoint(x: c.x + x * w, y: c.y + y * w)
    if i == 0 { p.move(to: pt) } else { p.addLine(to: pt) }
  }
  p.close()
  (on ? UIColor.white : UIColor(red: 1, green: 0.83, blue: 0.23, alpha: 1)).withAlphaComponent(dim).setFill()
  p.fill()
}

/// 마이크 그림
private func micIcon(in r: CGRect, dim: CGFloat) {
  let w = r.width * 0.5, x0 = r.midX - w / 2, y0 = r.midY - w / 2
  UIColor.white.withAlphaComponent(0.95 * dim).set()
  UIBezierPath(roundedRect: CGRect(x: x0 + w * 0.34, y: y0 + w * 0.06, width: w * 0.32, height: w * 0.52), cornerRadius: w * 0.16).fill()
  let arc = UIBezierPath(arcCenter: CGPoint(x: r.midX, y: y0 + w * 0.46), radius: w * 0.3, startAngle: 0, endAngle: .pi, clockwise: true)
  arc.move(to: CGPoint(x: r.midX, y: y0 + w * 0.76)); arc.addLine(to: CGPoint(x: r.midX, y: y0 + w * 0.94))
  arc.lineWidth = w * 0.1; arc.lineCapStyle = .round; arc.stroke()
}

/// 플레이스테이션 버튼 모양(글자 대신 그림). 평소엔 원래 색, 누르면 흰색.
private func psShape(_ kind: String, in r: CGRect, on: Bool, dim: CGFloat) {
  let w = r.width * 0.44, sw = w * 0.13, m = sw / 2
  let box = CGRect(x: r.midX - w / 2, y: r.midY - w / 2, width: w, height: w)
  let c: UIColor = on ? .white : {
    switch kind {
    case "@ps-tri": return UIColor(red: 0.18, green: 0.83, blue: 0.54, alpha: 1)
    case "@ps-cir": return UIColor(red: 1.0, green: 0.30, blue: 0.42, alpha: 1)
    case "@ps-cross": return UIColor(red: 0.30, green: 0.64, blue: 1.0, alpha: 1)
    default: return UIColor(red: 1.0, green: 0.44, blue: 0.81, alpha: 1)
    }
  }()
  c.withAlphaComponent(dim).setStroke()
  let p = UIBezierPath()
  switch kind {
  case "@ps-tri":
    let h = (w - 2 * m) * 0.866, top = box.minY + (w - h) / 2
    p.move(to: CGPoint(x: box.midX, y: top)); p.addLine(to: CGPoint(x: box.maxX - m, y: top + h))
    p.addLine(to: CGPoint(x: box.minX + m, y: top + h)); p.close()
  case "@ps-cir":
    p.append(UIBezierPath(ovalIn: box.insetBy(dx: m, dy: m)))
  case "@ps-cross":
    let a = w * 0.12
    p.move(to: CGPoint(x: box.minX + a, y: box.minY + a)); p.addLine(to: CGPoint(x: box.maxX - a, y: box.maxY - a))
    p.move(to: CGPoint(x: box.maxX - a, y: box.minY + a)); p.addLine(to: CGPoint(x: box.minX + a, y: box.maxY - a))
  default:
    p.append(UIBezierPath(rect: box.insetBy(dx: w * 0.1, dy: w * 0.1)))
  }
  p.lineWidth = sw; p.lineCapStyle = .round; p.lineJoinStyle = .round
  p.stroke()
}

struct ControlsRep: UIViewRepresentable {
  let emu: Emulator
  let system: GameSystem
  let big: Bool
  let stick: Bool
  let overlay: Bool
  func makeUIView(context: Context) -> ControlsView {
    let v = ControlsView()
    v.onMask = { emu.touchMask = $0 }
    v.onStick = { emu.touchStick = $0 }
    v.onRStick = { emu.touchRStick = $0 }
    return v
  }
  func updateUIView(_ v: ControlsView, context: Context) {
    if v.system != system { v.system = system }
    if v.big != big { v.big = big }
    if v.stick != stick { v.stick = stick }
    if v.overlay != overlay { v.overlay = overlay }
  }
}

// MARK: - 게임 화면

struct PlayView: View {
  let game: Game
  @EnvironmentObject var model: AppModel
  @Environment(\.scenePhase) private var phase
  @State private var emu = Emulator()
  @State private var toast: String?
  @State private var toastGen = 0
  @State private var panel = false
  @State private var slots: [SaveSlot] = []
  @State private var canSave = true
  @State private var syncing = false
  /// 패드로 실제로 누르기 시작하면 true(시뮬레이터의 가상 패드처럼 붙어만 있는 건 무시)
  @State private var padConnected = false
  @AppStorage("bigButtons") private var big = false
  @AppStorage("joystick") private var stick = false
  /// 화면 돌리기(게임마다 기억)
  @State private var rotation = 0
  private var rotKey: String { "rotation." + (game.path as NSString).lastPathComponent }

  var body: some View {
    GeometryReader { geo in
      let landscape = geo.size.width > geo.size.height
      ZStack(alignment: .top) {
        Color.black.ignoresSafeArea()
        if landscape || padConnected {
          ScreenRep(emu: emu, touch: game.system.hasTouch, rotation: rotation)
          if !padConnected { ControlsRep(emu: emu, system: game.system, big: big, stick: stick, overlay: true) }
        } else {
          LinearGradient(colors: game.system.gradient, startPoint: .top, endPoint: .bottom).ignoresSafeArea()
          VStack(spacing: 0) {
            ScreenRep(emu: emu, alignTop: true, touch: game.system.hasTouch, rotation: rotation)
              // DS 는 위아래 두 화면이라 세로로 길다: 화면 몫을 더 준다(옆으로 돌리면 보통 비율)
              .frame(height: game.system == .nds && rotation % 2 == 0 ? min(geo.size.width * 1.5, geo.size.height * 0.6) : min(geo.size.width * 0.9, geo.size.height * 0.48))
            ControlsRep(emu: emu, system: game.system, big: big, stick: stick, overlay: false)
          }
        }
        // 가로 화면에선 L/R 버튼과 안 겹치게 가운데 위
        HStack {
          if !landscape || padConnected { Spacer() }
          Button { setPanel(true) } label: {
            Text("메뉴").font(.system(size: 13, weight: .bold)).foregroundColor(.white)
              .padding(.horizontal, 14).padding(.vertical, 7)
              .background(Color.white.opacity(0.28), in: Capsule())
          }.buttonStyle(.plain)
        }.padding(.horizontal, 12).padding(.top, 6)
        if panel {
          SlotPanel(slots: slots, canSave: canSave, big: $big, stick: $stick, rotation: $rotation, padConnected: padConnected,
                    onSave: { id in emu.save(slot: id) { slots = emu.slots() } },
                    onLoad: { id in emu.load(slot: id); setPanel(false) },
                    onDelete: { id in emu.delete(slot: id); slots = emu.slots() },
                    onClose: { setPanel(false) },
                    onQuit: { model.playing = nil })
            .transition(.opacity)
        }
        if let t = toast {
          Text(t).font(.system(size: 14, weight: .bold)).foregroundColor(.white)
            .padding(.horizontal, 16).padding(.vertical, 8)
            .background(accent, in: Capsule()).padding(.top, 48)
            .transition(.opacity)
            .allowsHitTesting(false)
        }
      }
    }
    .statusBarHidden(true)
    .persistentSystemOverlays(.hidden)
    .onChange(of: rotation) { r in UserDefaults.standard.set(r, forKey: rotKey) }
    .onAppear {
      rotation = UserDefaults.standard.integer(forKey: rotKey)
      emu.onToast = { show($0) }
      emu.onMenu = { setPanel(!panel) }
      emu.onPadUsed = { if !padConnected { padConnected = true; show("게임패드로 하는 중") } }
      // 동기화를 켰으면 다른 기기 저장부터 받고 시작한다
      guard Sync.enabled else { return begin() }
      syncing = true
      let rom = (game.path as NSString).lastPathComponent
      DispatchQueue.global(qos: .userInitiated).async {
        let r = Sync.syncGame(savesDir: Emulator.savesDir, romFile: rom, pull: true)
        DispatchQueue.main.async {
          syncing = false
          guard model.playing?.path == game.path else { return }
          begin()
          if r == nil { show("동기화 서버에 못 닿아서 이 기기 저장으로 시작해요") }
        }
      }
    }
    .overlay { if syncing { syncOverlay } }
    .onDisappear { emu.stop() }
    .onChange(of: phase) { p in emu.setBackground(p != .active) }
    .onReceive(NotificationCenter.default.publisher(for: .GCControllerDidConnect)) { _ in updatePad() }
    .onReceive(NotificationCenter.default.publisher(for: .GCControllerDidDisconnect)) { _ in updatePad() }
  }

  var syncOverlay: some View {
    ZStack {
      Color.black.opacity(0.5).ignoresSafeArea()
      VStack(spacing: 10) {
        ProgressView().tint(.white).scaleEffect(1.4)
        Text("다른 기기 저장을 받는 중").font(.system(size: 15, weight: .bold)).foregroundColor(.white)
      }
    }
  }

  func begin() {
      // 지난번에 나갈 때 자동 저장한 게 있으면 바로 고를 수 있게 목록부터 연다(게임은 멈춘 채로 시작)
      let openPanel = Emulator.hasAutoSave(rom: game.path) || CommandLine.arguments.contains("--panel")
      if let err = emu.start(rom: game.path, system: game.system, paused: openPanel) {
        model.playing = nil
        model.error = err
      } else if openPanel {
        setPanel(true)
      }
  }

  func updatePad() {
    let any = PlayView.realPad()
    if !any && padConnected { padConnected = false; emu.resetPadSeen(); show("게임패드 끊김") }
    else if any { show("게임패드 연결됨") }
  }

  /// 진짜 게임패드가 붙어 있는지. 시뮬레이터 같은 가상 패드는 빼고, 무엇이 잡혔는지 로그로 남긴다.
  static func realPad() -> Bool {
    let pads = GCController.controllers().filter { $0.extendedGamepad != nil }
    for c in pads { NSLog("[pad] \(c.vendorName ?? "-") / \(c.productCategory) snapshot=\(c.isSnapshot) attached=\(c.isAttachedToDevice)") }
    return pads.contains { !$0.isSnapshot && !$0.productCategory.lowercased().contains("simulator") && !($0.vendorName ?? "").lowercased().contains("simulator") }
  }

  func setPanel(_ open: Bool) {
    if open { slots = emu.slots(); canSave = emu.hasRun; emu.touchMask = 0 }
    emu.setPaused(open)
    withAnimation(.easeOut(duration: 0.15)) { panel = open }
  }

  func show(_ s: String) {
    toastGen += 1
    let g = toastGen
    withAnimation { toast = s }
    DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
      if g == toastGen { withAnimation { toast = nil } }
    }
  }
}

// MARK: - 저장 목록 + 설정

struct SlotPanel: View {
  let slots: [SaveSlot]
  let canSave: Bool
  @Binding var big: Bool
  @Binding var stick: Bool
  @Binding var rotation: Int
  let padConnected: Bool
  let onSave: (String) -> Void
  let onLoad: (String) -> Void
  let onDelete: (String) -> Void
  let onClose: () -> Void
  let onQuit: () -> Void
  @State private var overwrite: String?
  @State private var deleting: String?

  var body: some View {
    ZStack {
      Color.black.opacity(0.55).ignoresSafeArea().onTapGesture(perform: onClose)
      VStack(alignment: .leading, spacing: 12) {
        HStack {
          Text("저장 / 불러오기").font(.system(size: 20, weight: .heavy)).foregroundStyle(accent)
          Spacer()
          Button(action: onClose) {
            Text("계속하기").font(.system(size: 13, weight: .bold)).foregroundColor(.white)
              .padding(.horizontal, 14).padding(.vertical, 7).background(accent, in: Capsule())
          }.buttonStyle(.plain)
        }
        ScrollView {
          LazyVGrid(columns: [GridItem(.adaptive(minimum: 140), spacing: 10)], spacing: 10) {
            ForEach(slots) { s in
              SlotCard(slot: s, canSave: canSave,
                       onSave: { if s.date != nil { overwrite = s.id } else { onSave(s.id) } },
                       onLoad: { onLoad(s.id) })
                .contextMenu {
                  if s.date != nil { Button(role: .destructive) { deleting = s.id } label: { Label("지우기", systemImage: "trash") } }
                }
            }
          }.padding(2)
          VStack(alignment: .leading, spacing: 10) {
            if !padConnected {
              setting("버튼 크기", ["기존", "크게"], big ? 1 : 0) { big = $0 == 1 }
              setting("방향키", ["십자키", "조이스틱"], stick ? 1 : 0) { stick = $0 == 1 }
            }
            setting("화면 돌리기", ["그대로", "오른쪽", "거꾸로", "왼쪽"], rotation) { rotation = $0 }
            Button(action: onQuit) {
              Text("게임 나가기").font(.system(size: 14, weight: .bold))
                .foregroundColor(Color(red: 0.85, green: 0.15, blue: 0.40))
                .frame(maxWidth: .infinity).padding(.vertical, 10)
                .background(Color.white.opacity(0.85), in: Capsule())
            }.buttonStyle(.plain)
            Text("길게 누르면 지우기 · 나가면 자동 저장돼요")
              .font(.system(size: 11, weight: .medium)).foregroundColor(.black.opacity(0.45))
          }.padding(.top, 10)
        }
      }
      .padding(16)
      .frame(maxWidth: 760)
      .background(bgGradient, in: RoundedRectangle(cornerRadius: 22))
      .shadow(color: .black.opacity(0.3), radius: 30, y: 10)
      .padding(.horizontal, 14).padding(.vertical, 20)
    }
    .alert("덮어쓸까요?", isPresented: Binding(get: { overwrite != nil }, set: { if !$0 { overwrite = nil } })) {
      Button("덮어쓰기") { if let id = overwrite { onSave(id) }; overwrite = nil }
      Button("취소", role: .cancel) { overwrite = nil }
    } message: { Text("\(SaveSlot.name(overwrite ?? "1"))에 있던 저장이 지워져요.") }
    .alert("지울까요?", isPresented: Binding(get: { deleting != nil }, set: { if !$0 { deleting = nil } })) {
      Button("지우기", role: .destructive) { if let id = deleting { onDelete(id) }; deleting = nil }
      Button("취소", role: .cancel) { deleting = nil }
    } message: { Text("\(SaveSlot.name(deleting ?? "1"))을 지우면 되돌릴 수 없어요.") }
  }

  func setting(_ title: String, _ opts: [String], _ sel: Int, _ pick: @escaping (Int) -> Void) -> some View {
    HStack {
      Text(title).font(.system(size: 14, weight: .bold)).foregroundColor(.black.opacity(0.75))
      Spacer()
      HStack(spacing: 4) {
        ForEach(opts.indices, id: \.self) { i in
          Button { pick(i) } label: {
            Text(opts[i]).font(.system(size: 13, weight: .bold))
              .foregroundColor(i == sel ? .white : Color(red: 0.36, green: 0.2, blue: 0.75))
              .padding(.horizontal, 14).padding(.vertical, 7)
              .background(i == sel ? AnyShapeStyle(accent) : AnyShapeStyle(Color.clear), in: Capsule())
          }.buttonStyle(.plain)
        }
      }
      .padding(3)
      .background(Color(red: 0.93, green: 0.91, blue: 1.0), in: Capsule())
    }
  }
}

struct SlotCard: View {
  let slot: SaveSlot
  let canSave: Bool
  let onSave: () -> Void
  let onLoad: () -> Void

  static let fmt: DateFormatter = {
    let f = DateFormatter(); f.locale = Locale(identifier: "ko_KR"); f.dateFormat = "M월 d일 a h:mm"; return f
  }()

  var body: some View {
    VStack(alignment: .leading, spacing: 7) {
      ZStack {
        Color.black
        if let t = slot.thumb {
          Image(uiImage: t).resizable().interpolation(.none).aspectRatio(contentMode: .fit)
        } else {
          Text(slot.date == nil ? "비어 있음" : "화면 없음").font(.system(size: 12, weight: .semibold)).foregroundColor(.white.opacity(0.5))
        }
      }
      .aspectRatio(4 / 3, contentMode: .fit)
      .clipShape(RoundedRectangle(cornerRadius: 10))
      Text(SaveSlot.name(slot.id)).font(.system(size: 13, weight: .heavy)).foregroundColor(.black.opacity(0.8))
      Text(slot.date.map { SlotCard.fmt.string(from: $0) } ?? " ")
        .font(.system(size: 11, weight: .medium)).foregroundColor(.black.opacity(0.5)).lineLimit(1)
      HStack(spacing: 6) {
        if !slot.isAuto { pill("저장", filled: false, action: onSave).disabled(!canSave).opacity(canSave ? 1 : 0.35) }
        pill("불러오기", filled: true, action: onLoad).disabled(slot.date == nil).opacity(slot.date == nil ? 0.35 : 1)
      }
    }
    .padding(9)
    .background(Color.white.opacity(0.85), in: RoundedRectangle(cornerRadius: 14))
  }

  func pill(_ t: String, filled: Bool, action: @escaping () -> Void) -> some View {
    Button(action: action) {
      Text(t).font(.system(size: 12, weight: .bold)).lineLimit(1)
        .frame(maxWidth: .infinity).padding(.vertical, 7)
        .foregroundColor(filled ? .white : Color(red: 0.36, green: 0.2, blue: 0.75))
        .background(filled ? AnyShapeStyle(accent) : AnyShapeStyle(Color(red: 0.93, green: 0.91, blue: 1.0)), in: Capsule())
        .contentShape(Capsule())
    }.buttonStyle(.plain)
  }
}
