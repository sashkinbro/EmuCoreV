package com.sbro.emucorev.core.vita

import android.content.Context
import android.os.Build
import android.view.Surface
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.View
import org.libsdl.app.SDLSurface

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
        declareGuestFrameRate(holder)
    }

    /**
     * Tells the system the content is 60 fps fixed-rate, so it picks a panel mode that is a multiple of 60
     * (60/120) where one exists. Only a hint: on other panels the native vsync follower paces the guest.
     */
    private fun declareGuestFrameRate(holder: SurfaceHolder) {
        if (emulator?.isGameSession != true || !holder.surface.isValid) return
        try {
            when {
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.S -> holder.surface.setFrameRate(
                    60f, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE, Surface.CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS
                )
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.R -> holder.surface.setFrameRate(
                    60f, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE
                )
            }
        } catch (_: RuntimeException) {
            // frame-rate hints are optional
        }
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        setSurfaceStatus(false)
        super.surfaceDestroyed(holder)
    }

    external fun setSurfaceStatus(surfacePresent: Boolean)
}
