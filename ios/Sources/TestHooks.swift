import UIKit

/// 점검용 실행 인자. 화면이 잠겨 있거나 시뮬레이터에서도 화면을 파일로 남길 수 있게 앱이 직접 찍는다.
///   --shot <파일.png> <초>   (여러 번 줄 수 있음)
///   --size <폭>x<높이>       (맥 Catalyst 창 크기 고정)
///   --layout <기종>          (게임 없이 그 기종 터치 버튼 배치만 띄움, 예: n64)
enum TestHooks {
  static func run() {
    let a = CommandLine.arguments
    for (i, v) in a.enumerated() where v == "--shot" && i + 2 < a.count {
      let path = a[i + 1], sec = Double(a[i + 2]) ?? 3
      DispatchQueue.main.asyncAfter(deadline: .now() + sec) { shot(path) }
    }
    if let i = a.firstIndex(of: "--layout"), i + 1 < a.count, let sys = GameSystem(rawValue: a[i + 1]) {
      DispatchQueue.main.asyncAfter(deadline: .now() + 1) {
        guard let w = UIApplication.shared.connectedScenes.compactMap({ ($0 as? UIWindowScene)?.windows.first }).first else { return }
        let v = ControlsView(frame: CGRect(x: 0, y: w.bounds.height * 0.45, width: w.bounds.width, height: w.bounds.height * 0.55))
        v.backgroundColor = UIColor(red: 0.42, green: 0.27, blue: 0.75, alpha: 1)
        v.system = sys
        w.addSubview(v)
      }
    }
    if let i = a.firstIndex(of: "--size"), i + 1 < a.count {
      let p = a[i + 1].split(separator: "x").compactMap { Double($0) }
      if p.count == 2 {
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
          for case let s as UIWindowScene in UIApplication.shared.connectedScenes {
            let size = CGSize(width: p[0], height: p[1])
            s.sizeRestrictions?.minimumSize = size
            s.sizeRestrictions?.maximumSize = size
          }
        }
      }
    }
  }

  static func shot(_ path: String) {
    guard let w = UIApplication.shared.connectedScenes.compactMap({ ($0 as? UIWindowScene)?.windows.first }).first else { return }
    let img = UIGraphicsImageRenderer(bounds: w.bounds).image { ctx in w.layer.render(in: ctx.cgContext) }
    try? img.pngData()?.write(to: URL(fileURLWithPath: path))
    print("shot \(path)")
  }
}
