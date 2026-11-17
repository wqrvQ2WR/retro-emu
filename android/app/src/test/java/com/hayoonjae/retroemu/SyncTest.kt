package com.hayoonjae.retroemu

import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.io.File
import java.net.HttpURLConnection
import java.net.URL

/**
 * 동기화 서버(sync-server/server.py)를 맥에서 띄워 두고 돌리는 테스트.
 *   DATA=/tmp/x python3 sync-server/server.py
 * 서버가 없으면 건너뛴다. 서버 주소는 RETRO_SYNC_TEST 로 바꿀 수 있다.
 */
class SyncTest {
    private val server = System.getenv("RETRO_SYNC_TEST") ?: "http://127.0.0.1:8270/retro-sync"
    private val code = "TEST-" + System.nanoTime()

    private fun up() = runCatching {
        (URL("$server/health").openConnection() as HttpURLConnection).run { connectTimeout = 1000; responseCode == 200 }
    }.getOrDefault(false)

    private fun tmp() = kotlin.io.path.createTempDirectory("sync").toFile()

    @Test fun twoDevices() {
        assumeTrue("동기화 서버가 안 떠 있음", up())
        val a = tmp(); val b = tmp()
        val rom = "Pokemon Red.gb"
        File(a, "$rom.auto.state").writeBytes(ByteArray(1000) { 7 })
        File(a, "$rom.auto.png").writeText("png")
        File(a, "$rom.srm").writeText("sram")
        File(a, "Other.gb.1.state").writeText("x")   // 다른 게임은 안 올라간다
        File(a, "$rom.auto.state.tmp").writeText("x") // 쓰다 만 파일도
        val t = System.currentTimeMillis() - 60_000
        a.listFiles()!!.forEach { it.setLastModified(t) }

        assertEquals(3, Sync.syncDir(a, server, code, rom, pull = false))
        assertEquals(3, Sync.syncDir(b, server, code, rom, pull = true))
        assertArrayEquals(File(a, "$rom.auto.state").readBytes(), File(b, "$rom.auto.state").readBytes())
        assertTrue(Math.abs(File(b, "$rom.auto.state").lastModified() - t) < 1500)
        assertFalse(File(b, "Other.gb.1.state").exists())
        // 이미 맞춰져 있으면 아무것도 안 한다
        assertEquals(0, Sync.syncDir(b, server, code, rom, pull = true))

        // B 에서 새로 저장하면 A 가 받는다
        File(b, "$rom.1.state").writeText("slot1 from b")
        assertEquals(1, Sync.syncDir(b, server, code, rom, pull = false))
        assertEquals(1, Sync.syncDir(a, server, code, rom, pull = true))
        assertEquals("slot1 from b", File(a, "$rom.1.state").readText())

        // 다른 코드는 안 보인다
        val c = tmp()
        assertEquals(0, Sync.syncDir(c, server, "$code-other", rom, pull = true))
        assertEquals(0, c.listFiles()!!.size)
    }

    @Test fun keySameAsOtherApps() {
        // 맥·iOS(Swift)·윈도우(C)와 같은 칸을 써야 한다
        assertEquals("73777b3b96824b5e9324ff1763175810", Sync.key("TEST-1234"))
    }

    @Test fun normalizeAddress() {
        assertEquals("http://192.168.0.5:8270/retro-sync", Sync.normalize("192.168.0.5:8270"))
        assertEquals("http://192.168.0.5:8270/retro-sync", Sync.normalize(" http://192.168.0.5:8270/ "))
        assertEquals("https://pi.taild4dc8e.ts.net/retro-sync", Sync.normalize("https://pi.taild4dc8e.ts.net/retro-sync"))
        assertEquals(Sync.DEFAULT_SERVER, Sync.normalize(""))
    }

    @Test fun serverDown() {
        val d = tmp()
        assertNull(Sync.syncDir(d, "http://127.0.0.1:1/retro-sync", code, "a.gb", pull = true))
    }
}
