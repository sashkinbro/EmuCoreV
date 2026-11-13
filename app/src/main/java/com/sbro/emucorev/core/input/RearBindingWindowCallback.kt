package com.sbro.emucorev.core.input

import android.view.KeyEvent
import android.view.MotionEvent
import android.view.Window

/** Compose dialogs own a Window, so capture must run before that window dispatches input. */
class RearBindingWindowCallback(
    private val delegate: Window.Callback,
    private val input: RearTouchInput,
    private val dimensions: () -> Pair<Int, Int>
) : Window.Callback by delegate {
    override fun dispatchKeyEvent(event: KeyEvent): Boolean =
        if (input.capture != null && input.key(event)) true else delegate.dispatchKeyEvent(event)

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean = dispatchMotion(event, delegate::dispatchGenericMotionEvent)
    override fun dispatchTouchEvent(event: MotionEvent): Boolean = dispatchMotion(event, delegate::dispatchTouchEvent)

    private fun dispatchMotion(event: MotionEvent, forward: (MotionEvent) -> Boolean): Boolean {
        if (input.capture == null) return forward(event)
        val (width,height) = dimensions()
        val result = input.motion(event,width,height)
        return try { result.consumed || forward(result.event ?: event) } finally { result.event?.recycle() }
    }
}
