package com.hayoonjae.retroemu

import android.content.Context
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.security.MessageDigest

/**
 * 저장 동기화. 같은 동기화 코드를 넣은 기기끼리 saves/ 의 상태 저장·썸네일·배터리 세이브를 주고받는다.
 * 파일마다 수정 시각이 더 새 쪽이 이긴다. 롬은 파일 이름으로 맞춘다.
 * 규칙은 맥·iOS Sync.swift, 윈도우 sync.c 와 같다. 서버는 sync-server/server.py.
 * 네트워크를 쓰는 함수는 메인 스레드에서 부르지 말 것.
 */
object Sync {
    const val DEFAULT_SERVER = "https://pi.taild4dc8e.ts.net/retro-sync"
    private val SENDABLE = listOf(".state", ".png", ".srm")

    private fun prefs(ctx: Context) = ctx.getSharedPreferences("sync", Context.MODE_PRIVATE)
    fun code(ctx: Context) = prefs(ctx).getString("code", "") ?: ""
    fun setCode(ctx: Context, c: String) = prefs(ctx).edit().putString("code", c.trim().uppercase()).apply()
    /** 같은 와이파이의 맥(sync-server/install-mac.sh)이나 테일스케일 너머 파이 등 아무 주소나 */
    fun server(ctx: Context) = prefs(ctx).getString("server", null) ?: DEFAULT_SERVER
    fun setServer(ctx: Context, s: String?) = prefs(ctx).edit().putString("server", s?.let { normalize(it) }).apply()

    /** "192.168.0.5:8270" 처럼 넣어도 되게: http:// 붙이고, 경로가 없으면 /retro-sync, 끝 / 는 뗀다 */
    fun normalize(s: String): String {
        var t = s.trim()
        if (t.isEmpty()) return DEFAULT_SERVER
        if (!t.contains("://")) t = "http://$t"
        t = t.trimEnd('/')
        val path = runCatching { java.net.URI(t).path }.getOrNull()
        if (path.isNullOrEmpty() || path == "/") t += "/retro-sync"
        return t
    }
    fun enabled(ctx: Context) = code(ctx).length >= 4
    fun lastSync(ctx: Context) = prefs(ctx).getLong("last", 0L)

    fun key(code: String): String =
        MessageDigest.getInstance("SHA-256").digest("retro-emu:$code".toByteArray())
            .joinToString("") { "%02x".format(it) }.substring(0, 32)

    /** 새 코드(헷갈리는 글자 0/O/1/I 는 뺌) */
    fun newCode(): String {
        val a = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
        val s = (1..8).map { a.random() }.joinToString("")
        return s.take(4) + "-" + s.takeLast(4)
    }

    fun savesDir(ctx: Context) = File(ctx.filesDir, "saves").apply { mkdirs() }

    /** 한 게임 저장을 서버와 맞춘다. pull 이 false 면 올리기만. 바뀐 파일 수, 실패하면 null. */
    fun syncGame(ctx: Context, romFile: String, pull: Boolean): Int? {
        if (!enabled(ctx)) return 0
        val r = syncDir(savesDir(ctx), server(ctx), code(ctx), romFile, pull) ?: return null
        prefs(ctx).edit().putLong("last", System.currentTimeMillis()).apply()
        return r
    }

    /** syncGame 의 본체(Context 없이 돌아서 단위 테스트로 서버와 붙여 볼 수 있다) */
    fun syncDir(dir: File, server: String, code: String, romFile: String, pull: Boolean): Int? {
        dir.mkdirs()
        val prefix = "$romFile."
        val base = server + "/v1/" + key(code)
        val remote = list(base, prefix) ?: return null
        val local = HashMap<String, Long>()
        dir.listFiles()?.forEach { f ->
            if (f.name.startsWith(prefix) && SENDABLE.any { f.name.endsWith(it) }) local[f.name] = f.lastModified()
        }
        var changed = 0
        for (n in local.keys + remote.keys) {
            val l = local[n]; val r = remote[n]
            val f = File(dir, n)
            if (pull && r != null && (l == null || r.first > l + 1000)) {
                if (r.second) { if (l != null && f.delete()) changed++ }
                else {
                    val data = get(base, n) ?: continue
                    val tmp = File(dir, "$n.sync")
                    tmp.writeBytes(data)
                    tmp.setLastModified(r.first)
                    if (tmp.renameTo(f)) changed++
                }
            } else if (l != null && (r == null || l > r.first + 1000)) {
                if (put(base, n, f.readBytes(), l)) changed++
            }
        }
        return changed
    }

    /** 뒤에서 올리기만(게임 중·나갈 때) */
    fun pushLater(ctx: Context, romFile: String) {
        if (!enabled(ctx) || romFile.isEmpty()) return
        val app = ctx.applicationContext
        Thread({ runCatching { syncGame(app, romFile, false) } }, "sync").start()
    }

    /** 슬롯을 지웠다고 알린다(다른 기기에서도 지워지게) */
    fun deleted(ctx: Context, names: List<String>) {
        if (!enabled(ctx)) return
        val base = server(ctx) + "/v1/" + key(code(ctx))
        val now = System.currentTimeMillis()
        Thread({ names.forEach { runCatching { request("$base/f/${esc(it)}", "DELETE", mtime = now) } } }, "sync").start()
    }

    fun check(ctx: Context) = runCatching { request(server(ctx) + "/health", "GET")?.first == 200 }.getOrDefault(false)

    // ---- HTTP ----

    private fun esc(s: String) = URLEncoder.encode(s, "UTF-8").replace("+", "%20")

    /** 이름 -> (수정 시각, 지워짐) */
    private fun list(base: String, prefix: String): Map<String, Pair<Long, Boolean>>? = runCatching {
        val (code, body) = request("$base/list?prefix=${esc(prefix)}", "GET") ?: return null
        if (code != 200) return null
        val files = JSONObject(String(body)).getJSONObject("files")
        files.keys().asSequence().associateWith { n ->
            val m = files.getJSONObject(n); m.getLong("mtime") to m.optBoolean("deleted", false)
        }
    }.getOrNull()

    private fun get(base: String, name: String): ByteArray? = runCatching {
        val (code, body) = request("$base/f/${esc(name)}", "GET", timeout = 30_000) ?: return null
        if (code == 200) body else null
    }.getOrNull()

    private fun put(base: String, name: String, data: ByteArray, mtime: Long) =
        runCatching { request("$base/f/${esc(name)}", "PUT", data, mtime, 30_000)?.first == 200 }.getOrDefault(false)

    private fun request(url: String, method: String, body: ByteArray? = null, mtime: Long? = null, timeout: Int = 6_000): Pair<Int, ByteArray>? {
        val c = URL(url).openConnection() as HttpURLConnection
        try {
            c.requestMethod = method
            c.connectTimeout = timeout; c.readTimeout = timeout
            if (mtime != null) c.setRequestProperty("X-Mtime", mtime.toString())
            if (body != null) {
                c.doOutput = true
                c.setFixedLengthStreamingMode(body.size)
                c.outputStream.use { it.write(body) }
            }
            val code = c.responseCode
            val data = (if (code < 400) c.inputStream else c.errorStream)?.use { it.readBytes() } ?: ByteArray(0)
            return code to data
        } finally { c.disconnect() }
    }
}
