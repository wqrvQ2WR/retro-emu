package com.hayoonjae.retroemu

import android.content.Context
import android.hardware.input.InputManager
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent

/**
 * 블루투스/USB 게임패드. 액티비티가 키·축 이벤트를 넘겨주면 지금 도는 엔진에 넣는다.
 * 버튼은 위치 기준(아래=B, 오른쪽=A, 왼쪽=Y, 위=X)이라 엑박·플스·닌텐도 패드 모두 같은 자리로 눌린다.
 */
object Gamepad {
    @Volatile var engine: Engine? = null
    /** 지금 게임이 아날로그 스틱 기종인지(게임 화면이 정해 줌) */
    @Volatile var analog = false
    /** 셀렉트+스타트 같이 누르기, 또는 패드의 홈(모드) 버튼 */
    var onMenu: (() -> Unit)? = null

    private var keys = 0
    private var axes = 0

    private fun bitFor(code: Int): Int? = when (code) {
        KeyEvent.KEYCODE_BUTTON_A -> Pad.B
        KeyEvent.KEYCODE_BUTTON_B -> Pad.A
        KeyEvent.KEYCODE_BUTTON_X -> Pad.Y
        KeyEvent.KEYCODE_BUTTON_Y -> Pad.X
        KeyEvent.KEYCODE_BUTTON_L1 -> Pad.L
        KeyEvent.KEYCODE_BUTTON_R1 -> Pad.R
        KeyEvent.KEYCODE_BUTTON_L2 -> Pad.L2
        KeyEvent.KEYCODE_BUTTON_R2 -> Pad.R2
        KeyEvent.KEYCODE_BUTTON_START -> Pad.START
        KeyEvent.KEYCODE_BUTTON_SELECT -> Pad.SELECT
        KeyEvent.KEYCODE_DPAD_UP -> Pad.UP
        KeyEvent.KEYCODE_DPAD_DOWN -> Pad.DOWN
        KeyEvent.KEYCODE_DPAD_LEFT -> Pad.LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT -> Pad.RIGHT
        else -> null
    }

    private fun push() { engine?.padMask = keys or axes }

    fun reset() { keys = 0; axes = 0; push() }

    /** 처리했으면 true */
    fun onKey(e: KeyEvent): Boolean {
        if (engine == null) return false
        if (e.keyCode == KeyEvent.KEYCODE_BUTTON_MODE) {
            if (e.action == KeyEvent.ACTION_DOWN && e.repeatCount == 0) onMenu?.invoke()
            return true
        }
        val b = bitFor(e.keyCode) ?: return false
        when (e.action) {
            KeyEvent.ACTION_DOWN -> keys = keys or (1 shl b)
            KeyEvent.ACTION_UP -> keys = keys and (1 shl b).inv()
        }
        val combo = (1 shl Pad.SELECT) or (1 shl Pad.START)
        if (e.action == KeyEvent.ACTION_DOWN && keys and combo == combo) {
            keys = 0; push(); onMenu?.invoke(); return true
        }
        push()
        return true
    }

    fun onMotion(e: MotionEvent): Boolean {
        if (engine == null || e.action != MotionEvent.ACTION_MOVE) return false
        if (e.source and InputDevice.SOURCE_JOYSTICK != InputDevice.SOURCE_JOYSTICK) return false
        val hx = e.getAxisValue(MotionEvent.AXIS_HAT_X); val hy = e.getAxisValue(MotionEvent.AXIS_HAT_Y)
        val x = e.getAxisValue(MotionEvent.AXIS_X); val y = e.getAxisValue(MotionEvent.AXIS_Y)
        // 아날로그 기종(N64·게임큐브·PSP·PS2)은 스틱을 그대로 아날로그로, 오른쪽 스틱은 Z/RZ
        val eng = engine
        if (eng != null && analog) {
            eng.setAnalog(0, x, y); eng.setAnalog(1, e.getAxisValue(MotionEvent.AXIS_Z), e.getAxisValue(MotionEvent.AXIS_RZ))
        }
        val sx = if (analog) 0f else x; val sy = if (analog) 0f else y
        val lt = maxOf(e.getAxisValue(MotionEvent.AXIS_LTRIGGER), e.getAxisValue(MotionEvent.AXIS_BRAKE))
        val rt = maxOf(e.getAxisValue(MotionEvent.AXIS_RTRIGGER), e.getAxisValue(MotionEvent.AXIS_GAS))
        var m = 0
        if (hx < -0.5f || sx < -0.5f) m = m or (1 shl Pad.LEFT)
        if (hx > 0.5f || sx > 0.5f) m = m or (1 shl Pad.RIGHT)
        if (hy < -0.5f || sy < -0.5f) m = m or (1 shl Pad.UP)
        if (hy > 0.5f || sy > 0.5f) m = m or (1 shl Pad.DOWN)
        if (lt > 0.5f) m = m or (1 shl Pad.L2)
        if (rt > 0.5f) m = m or (1 shl Pad.R2)
        axes = m
        push()
        return true
    }

    fun isPad(d: InputDevice?): Boolean {
        if (d == null || d.isVirtual) return false
        val s = d.sources
        return s and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD ||
            s and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK
    }

    /** 지금 붙어 있는 패드 이름(없으면 null) */
    fun connected(ctx: Context): String? {
        val im = ctx.getSystemService(InputManager::class.java)
        return im.inputDeviceIds.map { im.getInputDevice(it) }.firstOrNull { isPad(it) }?.name
    }
}
