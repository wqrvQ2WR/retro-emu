package com.hayoonjae.retroemu

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import java.io.File
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.locks.LockSupport

/** 코어 하나를 돌린다. 코어 호출은 에뮬 스레드에서만 한다(소리 읽기만 예외). */
class Engine(private val ctx: Context) {
    @Volatile private var running = false
    @Volatile var paused = false
    /** 앱이 뒤로 갔을 때(홈·잠금) */
    @Volatile private var background = false
    private val halted get() = paused || background
    private var emuThread: Thread? = null
    private var audioThread: Thread? = null
    private val tasks = ConcurrentLinkedQueue<() -> Unit>()
    private var mask = 0
    private val maskLock = Any()
    /** 게임패드 입력(Gamepad 가 씀). 터치 입력과 합쳐서 넣는다. */
    @Volatile var padMask = 0

    /** 화면 갱신 알림(Compose가 읽으면 다시 그림) */
    /** 화면 비율(바뀔 때만 Compose 가 다시 배치) */
    var aspect by mutableStateOf(4.0 / 3.0)
    /** 화면 뷰 픽셀 크기. 0이면 아직 서피스 없음 */
    @Volatile private var viewW = 0
    @Volatile private var viewH = 0
    /** 서피스가 새로 생겼으면 멈춰 있어도 마지막 화면을 다시 그린다 */
    @Volatile private var redraw = false
    var toast: String? by mutableStateOf(null)

    private var sramPath = ""
    /** saves/<롬 이름>. 슬롯 파일은 .<슬롯>.state / .<슬롯>.png */
    private var base = ""
    /** 롬 파일 이름(동기화 기준) */
    private var romFile = ""
    /** 이번에 켜고 나서 돈 프레임 수. 거의 안 했으면 나갈 때 자동 저장을 덮어쓰지 않는다. */
    @Volatile private var framesRun = 0
    /** 저장 목록이 바뀌면 올라간다(Compose 가 다시 읽게) */
    var slotsVersion by mutableIntStateOf(0)

    fun press(bit: Int, down: Boolean) = synchronized(maskLock) {
        mask = if (down) mask or (1 shl bit) else mask and (1 shl bit).inv()
    }
    fun releaseAll() = synchronized(maskLock) { mask = 0 }

    /** 실패하면 이유를 돌려준다. */
    fun start(rom: File, sys: GameSystem): String? {
        val core = File(ctx.applicationInfo.nativeLibraryDir, "lib${sys.coreLib}.so").path
        val sysDir = File(ctx.filesDir, "system").apply { mkdirs() }.path
        val saves = File(ctx.filesDir, "saves").apply { mkdirs() }.path
        if (!Native.open(core, sysDir, saves) || !Native.load(rom.path)) {
            val e = Native.error(); Native.close(); return e.ifEmpty { "열 수 없어요" }
        }
        sramPath = "$saves/${rom.name}.srm"
        base = "$saves/${rom.name}"
        romFile = rom.name
        // 예전 한 칸짜리 저장은 1번 슬롯으로 옮긴다
        File("$base.state").let { if (it.exists() && !stateFile("1").exists()) it.renameTo(stateFile("1")) }
        framesRun = 0
        Native.sramLoad(sramPath)
        aspect = Native.aspect()
        running = true
        emuThread = Thread({ emuLoop() }, "emu").apply { priority = Thread.MAX_PRIORITY; start() }
        audioThread = Thread({ audioLoop() }, "audio").apply { start() }
        return null
    }

    fun stop() {
        if (!running) return
        running = false
        emuThread?.join(); audioThread?.join()
        if (framesRun >= 300) saveSlot("auto")  // 나갈 때 자동 저장(5초 넘게 했을 때만)
        Native.sramSave(sramPath)
        Native.close()
        Sync.pushLater(ctx, romFile)
    }

    private fun stateFile(id: String) = File("$base.$id.state")
    private fun thumbFile(id: String) = File("$base.$id.png")

    fun slots(): List<SaveSlot> = SLOT_IDS.map { id ->
        val f = stateFile(id)
        if (!f.exists()) SaveSlot(id, null, null)
        else SaveSlot(id, f.lastModified(), thumbFile(id).takeIf { it.exists() }?.let { BitmapFactory.decodeFile(it.path) })
    }

    /** 에뮬 스레드(또는 에뮬 스레드가 멈춘 뒤)에서만 부른다 */
    private fun saveSlot(id: String): Boolean {
        if (!Native.stateSave(stateFile(id).path)) return false
        val shot = Native.size()?.let { sz ->
            Bitmap.createBitmap(sz[0], sz[1], Bitmap.Config.ARGB_8888).takeIf { Native.copyFrame(it) }
        }
        val t = thumbFile(id)
        if (shot != null) {
            val tmp = File(t.path + ".tmp")
            tmp.outputStream().use { shot.compress(Bitmap.CompressFormat.PNG, 100, it) }
            tmp.renameTo(t)
        } else t.delete()
        return true
    }

    /** 뒤로 가면 멈추고 자동 저장. 뒤에서 시스템이 앱을 꺼도 진행이 남는다. */
    fun setBackground(bg: Boolean) {
        background = bg
        if (bg && running) tasks.add {
            if (framesRun >= 300) { saveSlot("auto"); slotsVersion++ }
            Native.sramSave(sramPath)
            Sync.pushLater(ctx, romFile)
        }
    }

    /** 켜고 나서 한 프레임이라도 돌았는지(켜자마자 뜬 목록에서 빈 상태로 덮어쓰기 방지) */
    val hasRun get() = framesRun > 0

    fun save(id: String) = tasks.add {
        if (framesRun == 0) { show("게임을 시작한 뒤에 저장할 수 있어요"); return@add }
        val ok = saveSlot(id)
        show(if (ok) "${SaveSlot.name(id)}에 저장함" else "이 게임은 상태 저장을 못 해요")
        slotsVersion++
        if (ok) Sync.pushLater(ctx, romFile)
    }
    fun load(id: String) = tasks.add {
        val ok = Native.stateLoad(stateFile(id).path)
        show(if (ok) "${SaveSlot.name(id)} 불러옴" else "${SaveSlot.name(id)}이 비어 있어요")
    }
    fun delete(id: String) {
        stateFile(id).delete(); thumbFile(id).delete(); slotsVersion++
        Sync.deleted(ctx, listOf("$romFile.$id.state", "$romFile.$id.png"))
    }
    private fun show(s: String) { toast = s }

    private fun emuLoop() {
        val frameNs = (1e9 / Native.fps()).toLong()
        var next = System.nanoTime()
        var lastSerial = -1L
        var n = 0
        while (running) {
            while (true) { (tasks.poll() ?: break)() }
            if (redraw) { redraw = false; present() }
            if (halted) { LockSupport.parkNanos(20_000_000); next = System.nanoTime(); continue }
            Native.setInput(synchronized(maskLock) { mask } or padMask)
            Native.run()
            framesRun++
            if (++n % 600 == 0) Native.sramSave(sramPath)
            val s = Native.serial()
            if (s != lastSerial) { lastSerial = s; present() }
            next += frameNs
            val now = System.nanoTime()
            if (next - now > 0) LockSupport.parkNanos(next - now) else if (now - next > 100_000_000) next = now
        }
    }

    /** SurfaceView 의 서피스가 생기거나 바뀌거나 없어질 때(메인 스레드) */
    fun setSurface(surface: android.view.Surface?, w: Int, h: Int) {
        Native.setSurface(surface)
        viewW = w; viewH = h
        redraw = surface != null
    }

    private fun present() {
        if (viewW > 0) Native.present(viewW, viewH)
        val a = Native.aspect()
        if (a != aspect) aspect = a
    }

    private fun audioLoop() {
        val rate = Native.sampleRate().toInt()
        val minBuf = AudioTrack.getMinBufferSize(rate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT)
        val track = AudioTrack.Builder()
            .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build())
            .setAudioFormat(AudioFormat.Builder().setSampleRate(rate).setEncoding(AudioFormat.ENCODING_PCM_16BIT).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build())
            .setTransferMode(AudioTrack.MODE_STREAM)
            .setBufferSizeInBytes(maxOf(minBuf, rate / 10 * 4))
            .setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
            .build()
        val buf = ShortArray(512 * 2)
        track.play()
        while (running) {
            if (halted) { track.pause(); while (halted && running) Thread.sleep(30); track.play(); continue }
            Native.audioRead(buf, 512)
            track.write(buf, 0, buf.size)
        }
        track.stop(); track.release()
    }
}

val SLOT_IDS = listOf("auto", "1", "2", "3", "4", "5", "6")

class SaveSlot(val id: String, val time: Long?, val thumb: Bitmap?) {
    val isAuto get() = id == "auto"
    companion object { fun name(id: String) = if (id == "auto") "자동 저장" else "${id}번 슬롯" }
}
