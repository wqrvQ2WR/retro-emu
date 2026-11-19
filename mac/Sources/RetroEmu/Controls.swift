import AppKit
import SwiftUI
import CRhost

// 동작 번호는 RH_* 와 같다: B0 Y1 Select2 Start3 위4 아래5 왼6 오7 A8 X9 L10 R11 L2 12 R2 13 L3 14 R3 15

/// 기종별 키 배정. 동작 번호 0~15 는 패드 버튼(RH_*), 16~19 는 오른쪽 스틱 위·아래·왼쪽·오른쪽.
/// 기종마다 UserDefaults "keymap.<기종>" 에 [동작: 키코드] 로 저장한다.
enum KeyMap {
  /// 메뉴용이라 게임 버튼으로 못 쓰는 키: Esc, F1, F2, F3
  static let reserved: Set<UInt16> = [53, 122, 120, 99]

  static let defaults: [Int: UInt16] = [Int: UInt16](uniqueKeysWithValues: [(Int, UInt16)]([
    (4, 126), (5, 125), (6, 123), (7, 124),
    (0, 6), (8, 7), (1, 0), (9, 1),          // Z X A S
    (10, 12), (11, 13), (12, 14), (13, 15),   // Q W E R
    (3, 36), (2, 48),                           // Return, Tab
    (16, 34), (17, 40), (18, 38), (19, 37),                          // I K J L
  ]))

  /// 기종별 기본 키. PSP 는 오른쪽 스틱이 없어서 I/J/K/L 을 왼쪽 스틱(20~23)으로(PPSSPP 와 같음).
  /// 스틱이 주된 기종(N64·게임큐브)이 아닌 아날로그 기종은 왼쪽 스틱 줄이 따로 생긴다.
  static func defaults(for s: GameSystem) -> [Int: UInt16] {
    var m = defaults
    if s == .psp { for i in 0..<4 { m[20 + i] = m[16 + i]; m[16 + i] = nil } }
    return m
  }

  static func load(_ s: GameSystem) -> [Int: UInt16] {
    guard let d = UserDefaults.standard.dictionary(forKey: "keymap.\(s.rawValue)") as? [String: Int] else { return defaults(for: s) }
    var m: [Int: UInt16] = [:]
    for (k, v) in d { if let a = Int(k) { m[a] = UInt16(v) } }
    return m
  }

  static func save(_ m: [Int: UInt16], for s: GameSystem) {
    var d: [String: Int] = [:]
    for (a, k) in m { d[String(a)] = Int(k) }
    UserDefaults.standard.set(d, forKey: "keymap.\(s.rawValue)")
  }

  static func reset(_ s: GameSystem) { UserDefaults.standard.removeObject(forKey: "keymap.\(s.rawValue)") }

  /// 키 -> 동작(같은 키를 두 동작에 못 붙이므로 하나)
  static func lookup(_ m: [Int: UInt16]) -> [UInt16: Int] {
    var r: [UInt16: Int] = [:]
    for (a, k) in m { r[k] = a }
    return r
  }

  /// 한 동작에 키를 붙인다. 그 키를 쓰던 다른 동작은 비운다.
  static func assign(_ key: UInt16, to action: Int, in m: inout [Int: UInt16]) {
    for (a, k) in m where k == key { m[a] = nil }
    m[action] = key
  }

  /// 키 이름(맥 가상 키코드)
  static func name(_ k: UInt16) -> String {
    let t: [UInt16: String] = [
      0: "A", 1: "S", 2: "D", 3: "F", 4: "H", 5: "G", 6: "Z", 7: "X", 8: "C", 9: "V", 11: "B", 12: "Q", 13: "W", 14: "E",
      15: "R", 16: "Y", 17: "T", 18: "1", 19: "2", 20: "3", 21: "4", 22: "6", 23: "5", 24: "=", 25: "9", 26: "7", 27: "-",
      28: "8", 29: "0", 30: "]", 31: "O", 32: "U", 33: "[", 34: "I", 35: "P", 36: "Return", 37: "L", 38: "J", 39: "'",
      40: "K", 41: ";", 42: "\\", 43: ",", 44: "/", 45: "N", 46: "M", 47: ".", 48: "Tab", 49: "Space", 50: "`",
      51: "Delete", 56: "Shift", 57: "Caps Lock", 58: "Option", 59: "Control", 60: "오른쪽 Shift", 61: "오른쪽 Option",
      62: "오른쪽 Control", 55: "Command", 54: "오른쪽 Command", 76: "Enter", 117: "Del",
      115: "Home", 119: "End", 116: "Page Up", 121: "Page Down",
      123: "←", 124: "→", 125: "↓", 126: "↑",
      65: "숫자 .", 67: "숫자 *", 69: "숫자 +", 75: "숫자 /", 78: "숫자 -", 81: "숫자 =",
      82: "숫자 0", 83: "숫자 1", 84: "숫자 2", 85: "숫자 3", 86: "숫자 4", 87: "숫자 5", 88: "숫자 6", 89: "숫자 7",
      91: "숫자 8", 92: "숫자 9",
      96: "F5", 97: "F6", 98: "F7", 100: "F8", 101: "F9", 109: "F10", 103: "F11", 111: "F12",
    ]
    return t[k] ?? "키 \(k)"
  }

  /// 조작법 화면 줄: (동작, 이름). 이름은 코어가 알려 준 그대로 쓰고, 없으면 일반 패드 이름.
  static func rows(_ s: GameSystem) -> [(Int, String)] {
    let generic: [Int: String] = [Int: String](uniqueKeysWithValues: [(Int, String)]([
      (4, "위"), (5, "아래"), (6, "왼쪽"), (7, "오른쪽"),
      (0, "B"), (8, "A"), (1, "Y"), (9, "X"),
      (10, "L"), (11, "R"), (12, "L2"), (13, "R2"),
      (3, "Start"), (2, "Select"),
    ]))
    let order = [4, 5, 6, 7, 8, 0, 9, 1,
                 10, 11, 12, 13, 14, 15, 3, 2]
    let core = (0..<20).map { i -> String? in rh_button_name(Int32(i)).map { String(cString: $0) } }
    let hasCore = core.contains { $0 != nil }
    let dirs = ["위", "아래", "왼쪽", "오른쪽"]
    var out: [(Int, String)] = []
    for a in order {
      if s.stickFirst, a <= 7, a >= 4 {
        // 이 기종은 방향 키를 왼쪽 스틱으로 보낸다
        out.append((a, "왼쪽 스틱 " + dirs[a - 4]))
      } else if let n = core[a] {
        out.append((a, n))
      } else if !hasCore, let n = generic[a] {
        out.append((a, n))
      }
    }
    if s.hasAnalog && !s.stickFirst {
      for i in 0..<4 { out.append((20 + i, "왼쪽 스틱 " + dirs[i])) }
    }
    if s.hasAnalog && s != .psp {   // PSP 는 오른쪽 스틱이 없다(코어가 이름만 알려 줌)
      for i in 0..<4 {
        let base = core[16 + i] ?? "오른쪽 스틱"
        out.append((16 + i, base + " " + dirs[i]))
      }
    }
    return out
  }
}

/// 조작법: 기종별 버튼과 키. 줄을 누르고 새 키를 누르면 바뀐다.
struct ControlsPanel: View {
  let system: GameSystem
  let rows: [(Int, String)]
  let map: [Int: UInt16]
  @Binding var sel: Int
  /// 키를 기다리는 줄
  let capturing: Int?
  let onPick: (Int) -> Void
  let onReset: () -> Void
  let onClose: () -> Void

  var body: some View {
    ZStack {
      Color.black.opacity(0.55).ignoresSafeArea().onTapGesture(perform: onClose)
      VStack(alignment: .leading, spacing: 12) {
        HStack(alignment: .firstTextBaseline) {
          Text("조작법").font(.system(size: 22, weight: .heavy)).foregroundStyle(accent)
          Text(system.title).font(.system(size: 15, weight: .bold)).foregroundColor(.black.opacity(0.55))
          Spacer()
          Text(capturing != nil ? "새 키를 누르세요 · Esc 취소" : "줄을 누르고 새 키 · Esc 뒤로")
            .font(.system(size: 12, weight: .medium)).foregroundColor(.black.opacity(0.45))
        }
        ScrollViewReader { proxy in
          ScrollView {
            LazyVGrid(columns: [GridItem(.flexible(), spacing: 10), GridItem(.flexible(), spacing: 10)], spacing: 8) {
              ForEach(Array(rows.enumerated()), id: \.offset) { i, r in
                row(i, r.0, r.1).id(i)
              }
            }.padding(2)
          }
          .onChange(of: sel) { v in withAnimation { proxy.scrollTo(v) } }
        }
        HStack {
          Text("Esc 메뉴 · F1 저장 · F2 불러오기 · F3 저장 목록은 고정이에요")
            .font(.system(size: 12, weight: .medium)).foregroundColor(.black.opacity(0.45))
          Spacer()
          Button(action: onReset) {
            Text("기본값으로").font(.system(size: 13, weight: .bold))
              .padding(.horizontal, 16).padding(.vertical, 8)
              .foregroundColor(Color(red: 0.36, green: 0.2, blue: 0.75))
              .background(Color(red: 0.93, green: 0.91, blue: 1.0), in: Capsule())
              .contentShape(Capsule())
          }.buttonStyle(.plain)
        }
      }
      .padding(22)
      .frame(maxWidth: 640, maxHeight: 620)
      .background(bgGradient, in: RoundedRectangle(cornerRadius: 24))
      .shadow(color: .black.opacity(0.3), radius: 30, y: 10)
      .padding(28)
    }
  }

  func row(_ i: Int, _ action: Int, _ title: String) -> some View {
    let waiting = capturing == action
    return Button { onPick(i) } label: {
      HStack(spacing: 8) {
        Text(title).font(.system(size: 13, weight: .semibold)).foregroundColor(.black.opacity(0.8))
          .lineLimit(1).truncationMode(.tail)
        Spacer(minLength: 6)
        Text(waiting ? "누르세요" : map[action].map(KeyMap.name) ?? "없음")
          .font(.system(size: 13, weight: .bold))
          .foregroundColor(waiting ? .white : map[action] == nil ? .black.opacity(0.35) : Color(red: 0.36, green: 0.2, blue: 0.75))
          .padding(.horizontal, 12).padding(.vertical, 5)
          .frame(minWidth: 64)
          .background(waiting ? AnyShapeStyle(accent) : AnyShapeStyle(Color(red: 0.93, green: 0.91, blue: 1.0)), in: Capsule())
      }
      .padding(.horizontal, 12).padding(.vertical, 8)
      .background(Color.white.opacity(0.85), in: RoundedRectangle(cornerRadius: 12))
      .overlay(RoundedRectangle(cornerRadius: 12).strokeBorder(accent, lineWidth: i == sel ? 2.5 : 0))
      .contentShape(RoundedRectangle(cornerRadius: 12))
    }
    .buttonStyle(.plain)
  }
}
