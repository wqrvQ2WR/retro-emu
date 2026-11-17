package com.hayoonjae.retroemu

import android.graphics.Bitmap

/** C 호스트(rhost)와 이어지는 JNI. */
object Native {
    init { System.loadLibrary("rhostjni") }
    external fun open(core: String, sys: String, save: String): Boolean
    external fun load(rom: String): Boolean
    external fun close()
    external fun error(): String
    external fun run()
    external fun setInput(mask: Int)
    external fun setAnalog(stick: Int, x: Float, y: Float)
    external fun setPointer(x: Float, y: Float, down: Boolean)
    external fun fps(): Double
    external fun sampleRate(): Double
    external fun aspect(): Double
    external fun serial(): Long
    external fun size(): IntArray?
    external fun copyFrame(bmp: Bitmap): Boolean
    external fun setSurface(surface: android.view.Surface?)
    external fun present(maxW: Int, maxH: Int): Boolean
    external fun audioRead(buf: ShortArray, frames: Int): Int
    external fun audioBuffered(): Int
    external fun stateSave(path: String): Boolean
    external fun stateLoad(path: String): Boolean
    external fun sramSave(path: String): Boolean
    external fun sramLoad(path: String): Boolean
}

// libretro 조이패드 번호
object Pad {
    const val B = 0; const val Y = 1; const val SELECT = 2; const val START = 3
    const val UP = 4; const val DOWN = 5; const val LEFT = 6; const val RIGHT = 7
    const val A = 8; const val X = 9; const val L = 10; const val R = 11; const val L2 = 12; const val R2 = 13
}
