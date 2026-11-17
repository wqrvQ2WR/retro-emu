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
    PS1("플레이스테이션", "pcsx_rearmed_core", listOf("cue", "chd", "pbp", "m3u"), listOf(Color(0xFFFFB81A), Color(0xFFFF6133)));

    /** 카드에 넣는 게임기 사진(퍼블릭 도메인, assets/consoles/CREDITS.md) */
    val photo get() = when (this) {
        NES -> R.drawable.console_nes; GB -> R.drawable.console_gb; GBC -> R.drawable.console_gbc
        GBA -> R.drawable.console_gba; SNES -> R.drawable.console_snes; MD -> R.drawable.console_md
        PS1 -> R.drawable.console_ps1
    }
    /** 세로로 긴 휴대용 기기는 덜 넓게 */
    val tallPhoto get() = this == GB || this == GBC

    val hasXY get() = this == SNES || this == MD || this == PS1
    val hasShoulders get() = this == GBA || this == SNES || this == PS1
    val hasTriggers get() = this == PS1

    companion object {
        fun fromName(name: String): GameSystem? {
            val e = name.substringAfterLast('.', "").lowercase()
            return entries.firstOrNull { e in it.exts }
        }
        val allExts get() = entries.flatMap { it.exts }

        /** .bin 은 같은 폴더 .cue 가 가리키면 PS1 데이터(게임 아님), 아니면 메가드라이브 */
        fun of(f: File): GameSystem? {
            if (f.extension.lowercase() == "bin") {
                return if (f.name.lowercase() in cueReferenced(f.parentFile)) null else MD
            }
            return fromName(f.name)
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
