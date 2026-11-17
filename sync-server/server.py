#!/usr/bin/env python3
"""retro-emu 저장 동기화 서버. 파이썬 표준 라이브러리만 쓴다.

기기들이 같은 동기화 코드를 쓰면 같은 칸(key)을 본다. key 는 앱이 코드로 만든 해시라 서버는 코드를 모른다.
파일마다 수정 시각(ms)을 같이 두고, 더 새 것만 받는다. 지운 파일은 표시(deleted)만 남겨서 다른 기기에서도 지워지게 한다.

  GET    {base}/v1/<key>/list?prefix=<롬 파일 이름>.   -> {"files": {"이름": {"mtime": ms, "size": n, "deleted": bool}}}
  GET    {base}/v1/<key>/f/<이름>                     -> 내용 (X-Mtime 헤더)
  PUT    {base}/v1/<key>/f/<이름>   (X-Mtime 헤더)    -> 200, 서버 쪽이 더 새 거면 409
  DELETE {base}/v1/<key>/f/<이름>   (X-Mtime 헤더)    -> 200

환경변수: HOST(127.0.0.1), PORT(8270), BASE_PATH(/retro-sync), DATA(~/retro-sync-data)
"""
import json
import os
import re
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, unquote, urlparse

PORT = int(os.environ.get("PORT", "8270"))
BASE = os.environ.get("BASE_PATH", "/retro-sync").rstrip("/")
DATA = os.path.expanduser(os.environ.get("DATA", "~/retro-sync-data"))
MAX_FILE = 64 * 1024 * 1024        # 파일 하나(플레이스테이션 상태 저장도 이 안에 든다)
MAX_KEY = 2 * 1024 * 1024 * 1024   # 코드 하나당 전체
KEY_RE = re.compile(r"^[0-9a-f]{32}$")
lock = threading.Lock()


def safe_name(n):
    n = unquote(n)
    if not n or len(n) > 240 or "/" in n or "\\" in n or n.startswith(".") or "\x00" in n:
        return None
    return n


def meta_path(key):
    return os.path.join(DATA, key, ".meta.json")


def load_meta(key):
    try:
        with open(meta_path(key)) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def save_meta(key, meta):
    p = meta_path(key)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p + ".tmp", "w") as f:
        json.dump(meta, f)
    os.replace(p + ".tmp", p)


class H(BaseHTTPRequestHandler):
    server_version = "retro-sync/1"

    def log_message(self, fmt, *a):
        pass

    def send(self, code, body: object = b"", ctype="application/json", headers=None):
        if isinstance(body, (dict, list)):
            body = json.dumps(body, ensure_ascii=False).encode()
        elif isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def route(self):
        """(key, 동작, 이름, 쿼리) 를 돌려준다. Funnel 이 접두사를 떼든 말든 둘 다 받는다."""
        u = urlparse(self.path)
        path = u.path
        if BASE and path.startswith(BASE + "/"):
            path = path[len(BASE):]
        parts = path.strip("/").split("/")
        if parts == [""] or parts == ["health"]:
            return None, "health", None, None
        if len(parts) >= 3 and parts[0] == "v1" and KEY_RE.match(parts[1]):
            q = parse_qs(u.query)
            if parts[2] == "list" and len(parts) == 3:
                return parts[1], "list", None, q
            if parts[2] == "f" and len(parts) == 4:
                return parts[1], "file", safe_name(parts[3]), q
        return None, None, None, None

    def mtime_header(self):
        try:
            return int(self.headers.get("X-Mtime", ""))
        except ValueError:
            return None

    def do_GET(self):
        key, act, name, q = self.route()
        if act == "health":
            return self.send(200, {"ok": True})
        if act == "list":
            prefix = (q.get("prefix") or [""])[0]
            with lock:
                meta = load_meta(key)
            files = {n: m for n, m in meta.items() if n.startswith(prefix)}
            return self.send(200, {"files": files})
        if act == "file" and name:
            with lock:
                m = load_meta(key).get(name)
            p = os.path.join(DATA, key, name)
            if not m or m.get("deleted") or not os.path.exists(p):
                return self.send(404, {"error": "없음"})
            with open(p, "rb") as f:
                data = f.read()
            return self.send(200, data, "application/octet-stream", {"X-Mtime": str(m["mtime"])})
        self.send(404, {"error": "주소가 틀려요"})

    def do_PUT(self):
        key, act, name, _ = self.route()
        mt = self.mtime_header()
        if act != "file" or not name or mt is None:
            return self.send(400, {"error": "잘못된 요청"})
        n = int(self.headers.get("Content-Length", "0"))
        if n < 0 or n > MAX_FILE:
            return self.send(413, {"error": "파일이 너무 커요"})
        data = self.rfile.read(n)
        if len(data) != n:
            return self.send(400, {"error": "덜 받음"})
        with lock:
            meta = load_meta(key)
            cur = meta.get(name)
            if cur and cur["mtime"] > mt:
                return self.send(409, {"mtime": cur["mtime"]})
            total = sum(m.get("size", 0) for k, m in meta.items() if k != name and not m.get("deleted"))
            if total + n > MAX_KEY:
                return self.send(507, {"error": "저장 공간이 꽉 찼어요"})
            d = os.path.join(DATA, key)
            os.makedirs(d, exist_ok=True)
            p = os.path.join(d, name)
            with open(p + ".tmp", "wb") as f:
                f.write(data)
            os.replace(p + ".tmp", p)
            meta[name] = {"mtime": mt, "size": n, "deleted": False}
            save_meta(key, meta)
        self.send(200, {"ok": True})

    def do_DELETE(self):
        key, act, name, _ = self.route()
        mt = self.mtime_header()
        if act != "file" or not name or mt is None:
            return self.send(400, {"error": "잘못된 요청"})
        with lock:
            meta = load_meta(key)
            cur = meta.get(name)
            if cur and cur["mtime"] > mt:
                return self.send(409, {"mtime": cur["mtime"]})
            try:
                os.remove(os.path.join(DATA, key, name))
            except OSError:
                pass
            meta[name] = {"mtime": mt, "size": 0, "deleted": True}
            save_meta(key, meta)
        self.send(200, {"ok": True})


if __name__ == "__main__":
    os.makedirs(DATA, exist_ok=True)
    print(f"retro-sync :{PORT} base={BASE} data={DATA}", flush=True)
    ThreadingHTTPServer((os.environ.get("HOST", "127.0.0.1"), PORT), H).serve_forever()
