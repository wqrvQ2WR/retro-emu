import SwiftUI

/// 동기화 설정(맥·iOS 공용). 같은 코드를 다른 기기에도 넣으면 저장을 주고받는다.
struct SyncView: View {
  /// 지금 목록에 있는 롬 파일 경로들(지금 맞추기에 씀)
  let roms: [String]
  let onClose: () -> Void
  @State private var code = Sync.code
  @State private var status = ""
  @State private var busy = false
  @State private var last = Sync.lastSync

  static let fmt: DateFormatter = {
    let f = DateFormatter(); f.locale = Locale(identifier: "ko_KR"); f.dateFormat = "M월 d일 a h:mm"; return f
  }()

  var body: some View {
    VStack(alignment: .leading, spacing: 14) {
      HStack {
        Text("기기 간 저장 동기화").font(.system(size: 20, weight: .heavy)).foregroundStyle(accent)
        Spacer()
        Button(action: onClose) {
          Text("닫기").font(.system(size: 13, weight: .bold)).foregroundColor(.white)
            .padding(.horizontal, 14).padding(.vertical, 7).background(accent, in: Capsule())
        }.buttonStyle(.plain)
      }
      Text("같은 코드를 넣은 기기끼리 슬롯 저장, 자동 저장, 게임 안 세이브를 주고받아요. 게임을 켤 때 받고, 저장하거나 나갈 때 올려요. 롬 파일 이름이 기기마다 같아야 해요.")
        .font(.system(size: 13)).foregroundColor(.black.opacity(0.65)).fixedSize(horizontal: false, vertical: true)

      VStack(alignment: .leading, spacing: 8) {
        Text("동기화 코드").font(.system(size: 13, weight: .bold)).foregroundColor(.black.opacity(0.7))
        HStack(spacing: 8) {
          TextField("예: K7PM-2QXA", text: $code)
            .textFieldStyle(.plain)
            .font(.system(size: 18, weight: .bold, design: .monospaced))
            .foregroundColor(.black.opacity(0.85))
            .padding(.horizontal, 12).padding(.vertical, 9)
            .background(Color.white.opacity(0.9), in: RoundedRectangle(cornerRadius: 10))
            .autocorrectionDisabled()
          pill("새 코드", filled: false) { code = Sync.newCode() }
        }
        Text("처음 쓰는 기기에서 새 코드를 만들고, 다른 기기엔 그 코드를 그대로 넣어요. 코드를 아는 사람은 저장을 볼 수 있으니 남한테 알려주지 마세요.")
          .font(.system(size: 11)).foregroundColor(.black.opacity(0.5)).fixedSize(horizontal: false, vertical: true)
      }

      HStack(spacing: 8) {
        pill(Sync.enabled && Sync.code == norm(code) ? "저장됨" : "코드 저장", filled: true) { apply() }
          .disabled(norm(code).count < 4 || busy)
          .opacity(norm(code).count < 4 ? 0.4 : 1)
        pill("지금 맞추기", filled: false) { syncNow() }
          .disabled(!Sync.enabled || busy).opacity(Sync.enabled ? 1 : 0.4)
        if Sync.enabled { pill("끄기", filled: false) { Sync.code = ""; code = ""; status = "동기화를 껐어요" } }
      }

      HStack(spacing: 8) {
        if busy { ProgressView().controlSize(.small) }
        Text(status.isEmpty ? (last.map { "마지막 동기화: " + SyncView.fmt.string(from: $0) } ?? "아직 동기화한 적 없어요") : status)
          .font(.system(size: 12, weight: .semibold)).foregroundColor(.black.opacity(0.55))
      }
    }
    .padding(20)
    .frame(maxWidth: 520)
    .background(bgGradient, in: RoundedRectangle(cornerRadius: 22))
    .shadow(color: .black.opacity(0.25), radius: 24, y: 8)
    .padding(18)
  }

  func norm(_ s: String) -> String { s.trimmingCharacters(in: .whitespacesAndNewlines).uppercased() }

  func apply() {
    Sync.code = code
    code = Sync.code
    syncNow()
  }

  /// 목록의 모든 게임 저장을 맞춘다
  func syncNow() {
    busy = true
    status = "서버와 맞추는 중"
    let files = roms.map { ($0 as NSString).lastPathComponent }
    DispatchQueue.global(qos: .userInitiated).async {
      var ok = Sync.check(), n = 0
      if ok {
        for f in files {
          if let c = Sync.syncGame(savesDir: Emulator.savesDir, romFile: f, pull: true) { n += c } else { ok = false }
        }
      }
      DispatchQueue.main.async {
        busy = false
        last = Sync.lastSync
        status = ok ? (n == 0 ? "이미 맞춰져 있어요" : "파일 \(n)개를 주고받았어요") : "동기화 서버에 못 닿았어요"
      }
    }
  }

  func pill(_ t: String, filled: Bool, action: @escaping () -> Void) -> some View {
    Button(action: action) {
      Text(t).font(.system(size: 13, weight: .bold)).lineLimit(1)
        .padding(.horizontal, 14).padding(.vertical, 9)
        .foregroundColor(filled ? .white : Color(red: 0.36, green: 0.2, blue: 0.75))
        .background(filled ? AnyShapeStyle(accent) : AnyShapeStyle(Color(red: 0.93, green: 0.91, blue: 1.0)), in: Capsule())
        .contentShape(Capsule())
    }.buttonStyle(.plain)
  }
}
