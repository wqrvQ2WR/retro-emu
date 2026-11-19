package com.hayoonjae.retroemu

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.toArgb
import androidx.core.content.pm.ShortcutInfoCompat
import androidx.core.content.pm.ShortcutManagerCompat
import androidx.core.graphics.drawable.IconCompat
import java.io.File

/** 홈 화면 바로가기: 누르면 그 게임을 바로 켠다. 인텐트에는 롬 파일 이름만 담는다. */
object Shortcuts {
    const val EXTRA_ROM = "rom"

    /** 바로가기로 들어온, 아직 안 켠 게임(롬 파일 이름) */
    var pending by mutableStateOf<String?>(null)

    fun take(intent: Intent?) {
        intent?.getStringExtra(EXTRA_ROM)?.let { pending = it; intent.removeExtra(EXTRA_ROM) }
    }

    fun supported(ctx: Context) = ShortcutManagerCompat.isRequestPinShortcutSupported(ctx)

    /** 홈 화면에 고정해 달라고 런처에 요청(확인 창은 런처가 띄운다) */
    fun pin(ctx: Context, rom: File, title: String): Boolean {
        val sys = GameSystem.of(rom) ?: return false
        val intent = Intent(ctx, MainActivity::class.java).setAction(Intent.ACTION_VIEW)
            .putExtra(EXTRA_ROM, rom.name)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
        val info = ShortcutInfoCompat.Builder(ctx, "rom:" + rom.name)
            .setShortLabel(title.take(24))
            .setLongLabel(title)
            .setIcon(IconCompat.createWithAdaptiveBitmap(icon(ctx, sys)))
            .setIntent(intent)
            .build()
        return ShortcutManagerCompat.requestPinShortcut(ctx, info, null)
    }

    /** 적응형 아이콘(432px): 기종 그라디언트 위에 게임기 사진. 런처가 가장자리를 잘라내므로 사진은 가운데 안전 영역에. */
    private fun icon(ctx: Context, sys: GameSystem): Bitmap {
        val n = 432
        val bmp = Bitmap.createBitmap(n, n, Bitmap.Config.ARGB_8888)
        val c = Canvas(bmp)
        val bg = Paint().apply {
            shader = LinearGradient(0f, 0f, n.toFloat(), n.toFloat(), sys.colors[0].toArgb(), sys.colors[1].toArgb(), Shader.TileMode.CLAMP)
        }
        c.drawRect(0f, 0f, n.toFloat(), n.toFloat(), bg)
        BitmapFactory.decodeResource(ctx.resources, sys.photo)?.let { p ->
            val box = 232f   // 안전 영역(가운데 288px) 안쪽
            val s = minOf(box / p.width, box / p.height)
            val w = p.width * s; val h = p.height * s
            val dst = RectF((n - w) / 2, (n - h) / 2, (n + w) / 2, (n + h) / 2)
            c.drawBitmap(p, null, dst, Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG))
        }
        return bmp
    }
}
