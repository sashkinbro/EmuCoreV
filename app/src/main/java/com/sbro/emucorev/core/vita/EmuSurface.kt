package com.sbro.emucorev.core.vita

import android.content.Context
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.View
import com.sbro.emucorev.core.sdl.SDLSurface

class EmuSurface(context: Context) : SDLSurface(context) {
    private val emulator = context as? Emulator

    private val doubleTapDetector = GestureDetector(
        context,
        object : GestureDetector.SimpleOnGestureListener() {
            override fun onDoubleTap(e: MotionEvent): Boolean {
                emulator?.requestOverlayMenuButtonReveal()
                return false
            }
        }
    ).apply { setIsLongpressEnabled(false) }

    override fun onTouch(v: View, event: MotionEvent): Boolean {
        doubleTapDetector.onTouchEvent(event)
        return super.onTouch(v, event)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        setSurfaceStatus(true)
        super.surfaceCreated(holder)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        setSurfaceStatus(false)
        super.surfaceDestroyed(holder)
    }

    external fun setSurfaceStatus(surfacePresent: Boolean)
}
