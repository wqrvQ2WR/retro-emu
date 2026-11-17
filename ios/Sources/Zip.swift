import Compression
import Foundation

/// 아주 작은 zip 풀기(저장 방식 0=그대로, 8=deflate). 폴더 구조는 버리고 파일 이름만 쓴다.
/// iOS 엔 unzip 명령이 없어서 직접 읽는다. zip64 는 안 된다.
enum Zip {
  static func extract(_ zip: URL, to dir: URL) -> [URL] {
    guard let d = try? Data(contentsOf: zip, options: .mappedIfSafe), d.count >= 22 else { return [] }
    return d.withUnsafeBytes { (raw: UnsafeRawBufferPointer) -> [URL] in
      let b = raw.bindMemory(to: UInt8.self)
      func u16(_ o: Int) -> Int { Int(b[o]) | Int(b[o + 1]) << 8 }
      func u32(_ o: Int) -> Int { u16(o) | u16(o + 2) << 16 }
      // 끝에서 중앙 디렉터리 끝 표시(EOCD)를 찾는다
      var e = d.count - 22
      let stop = max(0, d.count - 22 - 65535)
      while e >= stop && u32(e) != 0x06054b50 { e -= 1 }
      guard e >= stop else { return [] }
      let count = u16(e + 10)
      var p = u32(e + 16)
      var out: [URL] = []
      for _ in 0..<count {
        guard p + 46 <= d.count, u32(p) == 0x02014b50 else { break }
        let method = u16(p + 10), csize = u32(p + 20), usize = u32(p + 24)
        let nlen = u16(p + 28), xlen = u16(p + 30), clen = u16(p + 32), local = u32(p + 42)
        let nameData = Data(bytes: b.baseAddress! + p + 46, count: nlen)
        p += 46 + nlen + xlen + clen
        let full = String(data: nameData, encoding: .utf8) ?? String(decoding: nameData, as: UTF8.self)
        let name = (full as NSString).lastPathComponent
        if full.hasSuffix("/") || name.isEmpty || name.hasPrefix(".") || full.contains("__MACOSX") { continue }
        guard local + 30 <= d.count, u32(local) == 0x04034b50 else { continue }
        let start = local + 30 + u16(local + 26) + u16(local + 28)
        guard start + csize <= d.count else { continue }
        let src = b.baseAddress! + start
        var file: Data?
        if method == 0 {
          file = Data(bytes: src, count: csize)
        } else if method == 8, usize > 0, csize > 0 {
          var buf = Data(count: usize)
          let n = buf.withUnsafeMutableBytes { dst in
            compression_decode_buffer(dst.bindMemory(to: UInt8.self).baseAddress!, usize, src, csize, nil, COMPRESSION_ZLIB)
          }
          if n == usize { file = buf }
        }
        guard let f = file else { continue }
        let u = dir.appendingPathComponent(name)
        if (try? f.write(to: u, options: .atomic)) != nil { out.append(u) }
      }
      return out
    }
  }
}
