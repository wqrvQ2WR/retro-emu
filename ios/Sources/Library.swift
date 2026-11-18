import SwiftUI
import UniformTypeIdentifiers

let bgGradient = LinearGradient(
  colors: [Color(red: 1.0, green: 0.93, blue: 0.97), Color(red: 0.93, green: 0.92, blue: 1.0), Color(red: 0.90, green: 0.96, blue: 1.0)],
  startPoint: .topLeading, endPoint: .bottomTrailing)
let accent = LinearGradient(
  colors: [Color(red: 1.0, green: 0.30, blue: 0.62), Color(red: 0.61, green: 0.36, blue: 1.0), Color(red: 0.24, green: 0.66, blue: 1.0)],
  startPoint: .leading, endPoint: .trailing)

struct LibraryView: View {
  @EnvironmentObject var model: AppModel
  @State private var showSync = false
  @State private var picking = false
  @State private var removing: Game?
  @State private var unlockTarget: Game?
  @State private var lockedTap: Game?

  var body: some View {
    ZStack {
      bgGradient.ignoresSafeArea()
      VStack(alignment: .leading, spacing: 16) {
        HStack {
          Text("레트로 에뮬레이터")
            .font(.system(size: 26, weight: .heavy))
            .foregroundStyle(accent)
            .lineLimit(1).minimumScaleFactor(0.7)
          Spacer()
          Button { showSync = true } label: {
            Label(Sync.enabled ? "동기화 켜짐" : "동기화", systemImage: "arrow.triangle.2.circlepath")
              .font(.system(size: 14, weight: .bold))
              .padding(.horizontal, 14).padding(.vertical, 9)
              .background(Color.white.opacity(0.85), in: Capsule())
              .foregroundColor(Color(red: 0.36, green: 0.2, blue: 0.75))
          }.buttonStyle(.plain)
          Button { picking = true } label: {
            Label("롬 추가", systemImage: "plus")
              .font(.system(size: 14, weight: .bold))
              .padding(.horizontal, 14).padding(.vertical, 9)
              .background(accent, in: Capsule())
              .foregroundColor(.white)
          }.buttonStyle(.plain)
        }
        if model.games.isEmpty {
          emptyState
        } else {
          ScrollView {
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 158), spacing: 14)], spacing: 14) {
              ForEach(model.games) { g in
                Button { if model.isLocked(g) { lockedTap = g } else { model.playing = g } } label: { GameCard(game: g, locked: model.isLocked(g)) }
                  .buttonStyle(.plain)
                  .contextMenu {
                    if model.isLocked(g) { Button { unlockTarget = g } label: { Label("잠금 풀기", systemImage: "lock.open") } }
                    else { Button { model.setLocked(g, true) } label: { Label("잠그기", systemImage: "lock") } }
                    Button(role: .destructive) { removing = g } label: { Label("지우기", systemImage: "trash") }
                  }
              }
            }.padding(.bottom, 20)
          }
        }
      }
      .padding(.horizontal, 18).padding(.top, 12)
      if showSync {
        Color.black.opacity(0.45).ignoresSafeArea().onTapGesture { showSync = false }
        SyncView(roms: model.games.map { $0.path }) { showSync = false }
      }
    }
    .onAppear { if CommandLine.arguments.contains("--sync") { showSync = true } }  // 점검용
    .fileImporter(isPresented: $picking, allowedContentTypes: [.item], allowsMultipleSelection: true) { r in
      if case .success(let urls) = r { model.add(urls: urls) }
    }
    .alert("\(unlockTarget?.title ?? "") 잠금을 풀까요?", isPresented: Binding(get: { unlockTarget != nil }, set: { if !$0 { unlockTarget = nil } })) {
      Button("잠금 풀기", role: .destructive) { if let g = unlockTarget { model.setLocked(g, false) }; unlockTarget = nil }
      Button("취소", role: .cancel) { unlockTarget = nil }
    } message: { Text(lockWarning) }
    .alert("잠긴 게임이에요", isPresented: Binding(get: { lockedTap != nil }, set: { if !$0 { lockedTap = nil } })) {
      Button("확인", role: .cancel) { lockedTap = nil }
    } message: { Text("켜려면 길게 눌러서 잠금 풀기를 골라요.") }
    .alert("지울까요?", isPresented: Binding(get: { removing != nil }, set: { if !$0 { removing = nil } })) {
      Button("지우기", role: .destructive) { if let g = removing { model.remove(g) }; removing = nil }
      Button("취소", role: .cancel) { removing = nil }
    } message: { Text("\(removing?.title ?? "") 롬 파일을 지워요. 저장은 남아요.") }
  }

  var emptyState: some View {
    VStack(spacing: 12) {
      Spacer()
      Image(systemName: "square.and.arrow.down").font(.system(size: 42, weight: .bold)).foregroundStyle(accent)
      Text("롬 추가를 눌러 롬 파일을 골라요").font(.system(size: 17, weight: .semibold)).foregroundColor(.black.opacity(0.7))
      Text("파일 앱의 RetroEmu 폴더에 넣어도 돼요. zip 은 풀어서 넣어요.")
        .font(.system(size: 13)).foregroundColor(.black.opacity(0.55)).multilineTextAlignment(.center)
      Text(GameSystem.allCases.map { $0.title }.joined(separator: " · "))
        .font(.system(size: 12)).foregroundColor(.black.opacity(0.45)).multilineTextAlignment(.center)
      Spacer()
    }.frame(maxWidth: .infinity).padding(.horizontal, 10)
  }
}

/// 잠금을 풀 때 경고(맥·윈도우·안드로이드와 같은 문구)
let lockWarning = "잠금을 풀면 누구나 바로 켜서 할 수 있어요. 게임 시간을 지키려고 잠가 둔 거라면 풀지 마세요."

struct GameCard: View {
  let game: Game
  var locked = false
  var body: some View {
    ZStack(alignment: .bottomLeading) {
      if locked {
        Label("잠김", systemImage: "lock.fill")
          .font(.system(size: 11, weight: .heavy)).foregroundColor(.white)
          .padding(.horizontal, 9).padding(.vertical, 4)
          .background(Color.black.opacity(0.35), in: Capsule())
          .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
      }
      if let img = game.system.photo {
        GeometryReader { g in
          Image(uiImage: img).resizable().interpolation(.high).aspectRatio(contentMode: .fit)
            .frame(width: g.size.width * (game.system.tallPhoto ? 0.40 : 0.72), height: game.system.tallPhoto ? 74 : 62, alignment: .topTrailing)
            .shadow(color: .black.opacity(0.18), radius: 5, y: 3)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
        }
      }
      VStack(alignment: .leading, spacing: 3) {
        Text(game.system.title).font(.system(size: 11, weight: .bold)).foregroundColor(.white.opacity(0.9))
        Text(game.title).font(.system(size: 15, weight: .heavy)).foregroundColor(.white).lineLimit(2)
          .multilineTextAlignment(.leading)
      }
    }
    .frame(maxWidth: .infinity, minHeight: 124, maxHeight: 124, alignment: .leading)
    .padding(12)
    .background(LinearGradient(colors: game.system.gradient, startPoint: .topLeading, endPoint: .bottomTrailing),
                in: RoundedRectangle(cornerRadius: 18))
    .saturation(locked ? 0.35 : 1)
    .shadow(color: game.system.gradient[0].opacity(0.35), radius: 8, y: 4)
    .contentShape(RoundedRectangle(cornerRadius: 18))
  }
}
