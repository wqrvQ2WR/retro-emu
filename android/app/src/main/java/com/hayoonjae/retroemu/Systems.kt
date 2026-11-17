package com.hayoonjae.retroemu

import androidx.compose.ui.graphics.Color
import java.io.File

enum class GameSystem(val title: String, val coreLib: String, val exts: List<String>, val colors: List<Color>) {
    NES("패미컴", "fceumm_core", listOf("nes", "fds", "unf"), listOf(Color(0xFFFF4D66), Color(0xFFFF9E33))),
    GB("게임보이", "mgba_core", listOf("gb"), listOf(Color(0xFF73D940), Color(0xFF1AC28C))),
    GBC("게임보이 컬러", "mgba_core", listOf("gbc"), listOf(Color(0xFF9E5CFF), Color(0xFF3DA9FF))),
    GBA("게임보이 어드밴스", "mgba_core", listOf("gba"), listOf(Color(0xFF664DFF), Color(0xFFFF4D9E))),
    SNES("슈퍼패미컴", "snes9x_core", listOf("sfc", "smc", "swc", "fig"), listOf(Color(0xFF9E5CFF), Color(0xFFFF4D9E))),
    MD("메가드라이브", "genesis_plus_gx_core", listOf("md", "gen", "smd"), listOf(Color(0xFF1F8CFF), Color(0xFF33D9E6))),
    PS1("플레이스테이션", "pcsx_rearmed_core", listOf("cue", "chd", "pbp", "m3u"), listOf(Color(0xFFFFB81A), Color(0xFFFF6133))),
    N64("닌텐도 64", "mupen64plus_next_core", listOf("n64", "z64", "v64"), listOf(Color(0xFF26BF59), Color(0xFFFFCC26))),
    NDS("닌텐도 DS", "melonds_core", listOf("nds"), listOf(Color(0xFF40B3FF), Color(0xFF9E66FF))),
    GC("게임큐브", "dolphin_core", listOf("gcm", "rvz", "gcz", "ciso", "wbfs"), listOf(Color(0xFF734DF2), Color(0xFF40CCD9))),
    PSP("PSP", "ppsspp_core", listOf("cso"), listOf(Color(0xFF334DE6), Color(0xFFF24DB3))),
    PS2("플레이스테이션 2", "play_core", listOf(), listOf(Color(0xFF2666FF), Color(0xFF8C40F2)));

    /** 카드에 넣는 게임기 사진(퍼블릭 도메인, assets/consoles/CREDITS.md) */
    val photo get() = when (this) {
        NES -> R.drawable.console_nes; GB -> R.drawable.console_gb; GBC -> R.drawable.console_gbc
        GBA -> R.drawable.console_gba; SNES -> R.drawable.console_snes; MD -> R.drawable.console_md
        PS1 -> R.drawable.console_ps1; N64 -> R.drawable.console_n64; NDS -> R.drawable.console_nds
        GC -> R.drawable.console_gc; PSP -> R.drawable.console_psp; PS2 -> R.drawable.console_ps2
    }
    /** 세로로 긴 휴대용 기기는 덜 넓게 */
    val tallPhoto get() = this == GB || this == GBC || this == NDS || this == PS2

    val hasXY get() = this == SNES || this == MD || this == PS1 || this == NDS || this == GC || this == PSP || this == PS2 || this == N64
    val hasShoulders get() = this == GBA || this == SNES || this == PS1 || this == NDS || this == GC || this == PSP || this == PS2 || this == N64
    val hasTriggers get() = this == PS1 || this == PS2 || this == GC || this == N64
    /** 아날로그 스틱이 주된 조작(터치 스틱을 아날로그로 보낸다) */
    val hasAnalog get() = this == N64 || this == GC || this == PSP || this == PS2
    /** 아래 화면 터치 */
    val hasTouch get() = this == NDS
    /** 처음 켤 때 시스템 폴더로 복사할 앱 안 자료(assets/sysassets/<이름>) */
    val sysAssets get() = when (this) { PSP -> "PPSSPP"; GC -> "dolphin-emu"; else -> null }
    /** 버튼 이름(PS 계열은 도형) */
    fun label(bit: Int) = if (this == PS1 || this == PS2 || this == PSP) when (bit) { Pad.X -> "삼각"; Pad.Y -> "사각"; Pad.A -> "동그"; Pad.B -> "엑스"; else -> null } else null

    companion object {
        fun fromName(name: String): GameSystem? {
            val e = name.substringAfterLast('.', "").lowercase()
            return entries.firstOrNull { e in it.exts }
        }
        val allExts get() = entries.flatMap { it.exts }

        /** .bin 은 같은 폴더 .cue 가 가리키면 PS1 데이터(게임 아님), 아니면 메가드라이브 */
        fun of(f: File): GameSystem? {
            when (f.extension.lowercase()) { "iso" -> return sniffIso(f); "pbp" -> return sniffPbp(f) }
            if (f.extension.lowercase() == "bin") {
                return if (f.name.lowercase() in cueReferenced(f.parentFile)) null else MD
            }
            return fromName(f.name)
        }

        private fun bytes(f: File, off: Long, n: Int): ByteArray = runCatching {
            java.io.RandomAccessFile(f, "r").use { r -> r.seek(off); val b = ByteArray(n); val got = r.read(b); if (got < n) b.copyOf(maxOf(got, 0)) else b }
        }.getOrDefault(ByteArray(0))

        /** .iso: 게임큐브·위 표식 -> 게임큐브, ISO9660 시스템 이름 "PSP GAME" -> PSP, 그 밖 -> PS2 (맥·윈도우와 같은 규칙) */
        fun sniffIso(f: File): GameSystem {
            val h = bytes(f, 0, 0x20)
            if (h.size == 0x20) {
                val gc = h[0x1C] == 0xC2.toByte() && h[0x1D] == 0x33.toByte() && h[0x1E] == 0x9F.toByte() && h[0x1F] == 0x3D.toByte()
                val wii = h[0x18] == 0x5D.toByte() && h[0x19] == 0x1C.toByte() && h[0x1A] == 0x9E.toByte() && h[0x1B] == 0xA3.toByte()
                if (gc || wii) return GC
            }
            val v = bytes(f, 0x8000, 40)
            if (v.size == 40 && String(v, 1, 5, Charsets.US_ASCII) == "CD001" && String(v, 8, 8, Charsets.US_ASCII) == "PSP GAME") return PSP
            return PS2
        }

        /** .pbp: DATA.PSAR 머리가 PSISOIMG/PSTITLEIMG 면 PS1 게임, 아니면 PSP */
        fun sniffPbp(f: File): GameSystem {
            val h = bytes(f, 0, 0x28)
            if (h.size < 0x28 || h[0] != 0.toByte() || String(h, 1, 3, Charsets.US_ASCII) != "PBP") return PS1
            val off = (h[0x24].toLong() and 0xFF) or ((h[0x25].toLong() and 0xFF) shl 8) or ((h[0x26].toLong() and 0xFF) shl 16) or ((h[0x27].toLong() and 0xFF) shl 24)
            val tag = String(bytes(f, off, 8), Charsets.US_ASCII)
            return if (tag == "PSISOIMG" || tag.startsWith("PSTITLEI")) PS1 else PSP
        }

        fun cueReferenced(dir: File?): Set<String> {
            val out = HashSet<String>()
            dir?.listFiles()?.filter { it.extension.equals("cue", true) }?.forEach { c ->
                runCatching {
                    c.readLines(Charsets.ISO_8859_1).forEach { l ->
                        val t = l.trim()
                        if (t.startsWith("FILE", true)) {
                            val a = t.indexOf('"'); val b = t.lastIndexOf('"')
                            if (a >= 0 && b > a) out.add(t.substring(a + 1, b).lowercase())
                        }
                    }
                }
            }
            return out
        }
    }
}
