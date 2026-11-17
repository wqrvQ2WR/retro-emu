import CryptoKit
import Foundation

/// 저장 동기화(맥·iOS 공용, iOS 빌드도 이 파일을 같이 컴파일한다).
/// 같은 동기화 코드를 넣은 기기끼리 saves/ 의 상태 저장·썸네일·배터리 세이브를 주고받는다.
/// 파일마다 수정 시각이 더 새 쪽이 이긴다. 롬은 파일 이름으로 맞춘다(기기마다 같은 이름이어야 함).
/// 규칙은 안드로이드 Sync.kt, 윈도우 sync.c 와 같다. 서버는 sync-server/server.py.
enum Sync {
  static let defaultServer = "https://pi.taild4dc8e.ts.net/retro-sync"

  static var code: String {
    get { UserDefaults.standard.string(forKey: "syncCode") ?? "" }
    set { UserDefaults.standard.set(newValue.trimmingCharacters(in: .whitespacesAndNewlines).uppercased(), forKey: "syncCode") }
  }
  /// 같은 와이파이의 맥(sync-server/install-mac.sh)이나 테일스케일 너머 파이 등 아무 주소나 넣을 수 있다.
  /// 점검할 땐 RETROEMU_SYNC 환경변수가 이긴다.
  static var server: String {
    get { ProcessInfo.processInfo.environment["RETROEMU_SYNC"] ?? UserDefaults.standard.string(forKey: "syncServer") ?? defaultServer }
    set { UserDefaults.standard.set(normalize(newValue), forKey: "syncServer") }
  }

  /// "192.168.0.5:8270" 처럼 넣어도 되게: http:// 붙이고, 경로가 없으면 /retro-sync, 끝 / 는 뗀다
  static func normalize(_ s: String) -> String {
    var t = s.trimmingCharacters(in: .whitespacesAndNewlines)
    if t.isEmpty { return defaultServer }
    if !t.contains("://") { t = "http://" + t }
    while t.hasSuffix("/") { t.removeLast() }
    if let u = URL(string: t), u.path.isEmpty || u.path == "/" { t += "/retro-sync" }
    return t
  }

  /// 같은 와이파이에서 이 기기 주소(맥이 서버일 때 다른 기기에 알려줄 것)
  static func lanAddress() -> String? {
    var ifs: UnsafeMutablePointer<ifaddrs>?
    guard getifaddrs(&ifs) == 0, let first = ifs else { return nil }
    defer { freeifaddrs(ifs) }
    var out: String?
    for p in sequence(first: first, next: { $0.pointee.ifa_next }) {
      let a = p.pointee
      guard let sa = a.ifa_addr, sa.pointee.sa_family == UInt8(AF_INET) else { continue }
      let name = String(cString: a.ifa_name)
      guard name == "en0" || name == "en1" else { continue }
      var host = [CChar](repeating: 0, count: Int(NI_MAXHOST))
      if getnameinfo(sa, socklen_t(sa.pointee.sa_len), &host, socklen_t(host.count), nil, 0, NI_NUMERICHOST) == 0 {
        out = String(cString: host); if name == "en0" { break }
      }
    }
    return out
  }

  /// 이 기기에서 동기화 서버가 돌고 있는지(맥에서 install-mac.sh 로 깐 경우)
  static func localServerUp() -> Bool {
    request("http://127.0.0.1:8270/retro-sync/health", "GET", timeout: 1.5)?.0 == 200
  }
  static var enabled: Bool { code.count >= 4 }
  static var lastSync: Date? {
    get { UserDefaults.standard.object(forKey: "syncLast") as? Date }
    set { UserDefaults.standard.set(newValue, forKey: "syncLast") }
  }

  /// 코드 -> 서버에서 쓰는 칸 이름. 서버는 코드를 모른다.
  static func key(_ code: String) -> String {
    let d = SHA256.hash(data: Data(("retro-emu:" + code).utf8))
    return d.map { String(format: "%02x", $0) }.joined().prefix(32).description
  }

  /// 새 코드(헷갈리는 글자 0/O/1/I 는 뺌)
  static func newCode() -> String {
    let a = Array("ABCDEFGHJKLMNPQRSTUVWXYZ23456789")
    let s = String((0..<8).map { _ in a.randomElement()! })
    return s.prefix(4) + "-" + s.suffix(4)
  }

  private static let sendable = [".state", ".png", ".srm"]

  /// 한 게임의 저장을 서버와 맞춘다. pull 이 false 면 올리기만 한다(게임 중).
  /// 네트워크를 쓰므로 메인 스레드에서 부르지 말 것. 바뀐 파일 수를 돌려주고, 실패하면 nil.
  @discardableResult
  static func syncGame(savesDir: String, romFile: String, pull: Bool) -> Int? {
    guard enabled else { return 0 }
    let prefix = romFile + "."
    let base = server + "/v1/" + key(code)
    guard let remote = list(base, prefix: prefix) else { return nil }
    let fm = FileManager.default
    try? fm.createDirectory(atPath: savesDir, withIntermediateDirectories: true)
    var local: [String: Int64] = [:]
    for n in (try? fm.contentsOfDirectory(atPath: savesDir)) ?? [] where n.hasPrefix(prefix) && sendable.contains(where: { n.hasSuffix($0) }) {
      if let d = (try? fm.attributesOfItem(atPath: savesDir + "/" + n))?[.modificationDate] as? Date {
        local[n] = Int64(d.timeIntervalSince1970 * 1000)
      }
    }
    var changed = 0
    for n in Set(local.keys).union(remote.keys) {
      let l = local[n], r = remote[n]
      let path = savesDir + "/" + n
      if pull, let r, l == nil || r.mtime > l! + 1000 {
        if r.deleted {
          if l != nil { try? fm.removeItem(atPath: path); changed += 1 }
        } else if let data = get(base, n) {
          let tmp = path + ".sync"
          if (try? data.write(to: URL(fileURLWithPath: tmp))) != nil {
            try? fm.setAttributes([.modificationDate: Date(timeIntervalSince1970: Double(r.mtime) / 1000)], ofItemAtPath: tmp)
            if rename(tmp, path) == 0 { changed += 1 }  // 한 번에 바꿔치기(수정 시각 유지)
          }
        }
      } else if let l, r == nil || l > r!.mtime + 1000 {
        if let data = fm.contents(atPath: path), put(base, n, data, l) { changed += 1 }
      }
    }
    lastSync = Date()
    return changed
  }

  /// 슬롯을 지웠다고 서버에 알린다(다른 기기에서도 지워지게)
  static func deleted(romFile: String, names: [String]) {
    guard enabled else { return }
    let base = server + "/v1/" + key(code)
    let now = Int64(Date().timeIntervalSince1970 * 1000)
    DispatchQueue.global(qos: .utility).async {
      for n in names where n.hasPrefix(romFile + ".") { _ = request(base + "/f/" + esc(n), "DELETE", mtime: now) }
    }
  }

  /// 서버에 닿는지(설정 화면 확인용)
  static func check() -> Bool {
    request(server + "/health", "GET")?.0 == 200
  }

  // MARK: HTTP

  struct Remote { let mtime: Int64; let deleted: Bool }

  private static func esc(_ s: String) -> String {
    s.addingPercentEncoding(withAllowedCharacters: .alphanumerics.union(CharacterSet(charactersIn: "-._~"))) ?? s
  }

  private static func list(_ base: String, prefix: String) -> [String: Remote]? {
    guard let (code, data) = request(base + "/list?prefix=" + esc(prefix), "GET"), code == 200,
          let j = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
          let files = j["files"] as? [String: [String: Any]] else { return nil }
    var out: [String: Remote] = [:]
    for (n, m) in files {
      guard let mt = (m["mtime"] as? NSNumber)?.int64Value else { continue }
      out[n] = Remote(mtime: mt, deleted: (m["deleted"] as? Bool) ?? false)
    }
    return out
  }

  private static func get(_ base: String, _ name: String) -> Data? {
    guard let (code, data) = request(base + "/f/" + esc(name), "GET", timeout: 30), code == 200 else { return nil }
    return data
  }

  private static func put(_ base: String, _ name: String, _ data: Data, _ mtime: Int64) -> Bool {
    request(base + "/f/" + esc(name), "PUT", body: data, mtime: mtime, timeout: 30)?.0 == 200
  }

  private static func request(_ url: String, _ method: String, body: Data? = nil, mtime: Int64? = nil, timeout: Double = 6) -> (Int, Data)? {
    guard let u = URL(string: url) else { return nil }
    var r = URLRequest(url: u, timeoutInterval: timeout)
    r.httpMethod = method
    r.httpBody = body
    if let mtime { r.setValue(String(mtime), forHTTPHeaderField: "X-Mtime") }
    let sem = DispatchSemaphore(value: 0)
    var out: (Int, Data)?
    URLSession.shared.dataTask(with: r) { d, resp, _ in
      if let h = resp as? HTTPURLResponse { out = (h.statusCode, d ?? Data()) }
      sem.signal()
    }.resume()
    sem.wait()
    return out
  }
}
