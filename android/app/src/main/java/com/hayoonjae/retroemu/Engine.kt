package com.hayoonjae.retroemu

import android.content.Context
import android.graphics.Bitmap
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
    private var emuThread: Thread? = null
    private var audioThread: Thread? = null
    private val tasks = ConcurrentLinkedQueue<() -> Unit>()
    private var mask = 0
    private val maskLock = Any()

    /** 화면 갱신 알림(Compose가 읽으면 다시 그림) */
    var frameTick by mutableIntStateOf(0)
    @Volatile var bitmap: Bitmap? = null
    @Volatile var aspect = 4.0 / 3.0
    var toast: String? by mutableStateOf(null)

    private var front: Bitmap? = null
    private var back: Bitmap? = null
    private var sramPath = ""
    private var statePath = ""

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
        statePath = "$saves/${rom.name}.state"
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
        Native.sramSave(sramPath)
        Native.close()
    }

    fun saveState() = tasks.add { show(if (Native.stateSave(statePath)) "상태 저장함" else "상태 저장을 못 했어요") }
    fun loadState() = tasks.add { show(if (Native.stateLoad(statePath)) "상태 불러옴" else "저장한 상태가 없어요") }
    private fun show(s: String) { toast = s }

    private fun emuLoop() {
        val frameNs = (1e9 / Native.fps()).toLong()
        var next = System.nanoTime()
        var lastSerial = -1L
        var n = 0
        while (running) {
            while (true) { (tasks.poll() ?: break)() }
            if (paused) { LockSupport.parkNanos(20_000_000); next = System.nanoTime(); continue }
            Native.setInput(synchronized(maskLock) { mask })
            Native.run()
            if (++n % 600 == 0) Native.sramSave(sramPath)
            val s = Native.serial()
            if (s != lastSerial) { lastSerial = s; publish() }
            next += frameNs
            val now = System.nanoTime()
            if (next - now > 0) LockSupport.parkNanos(next - now) else if (now - next > 100_000_000) next = now
        }
    }

    private fun publish() {
        val sz = Native.size() ?: return
        var b = back
        if (b == null || b.width != sz[0] || b.height != sz[1]) {
            b = Bitmap.createBitmap(sz[0], sz[1], Bitmap.Config.ARGB_8888)
        }
        if (!Native.copyFrame(b)) return
        back = front; front = b
        bitmap = b
        aspect = Native.aspect()
        frameTick++
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
            if (paused) { track.pause(); while (paused && running) Thread.sleep(30); track.play(); continue }
            Native.audioRead(buf, 512)
            track.write(buf, 0, buf.size)
        }
        track.stop(); track.release()
    }
}
