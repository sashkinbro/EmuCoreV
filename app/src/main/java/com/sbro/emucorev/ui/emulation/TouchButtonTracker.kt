package com.sbro.emucorev.ui.emulation

/** Reference-counts fingers so lifting one cannot release another finger's button. */
internal class TouchButtonTracker {
    private val pointers = mutableMapOf<Int, Int>()

    fun update(pointerId: Int, button: Int?, emit: (Int, Boolean) -> Unit) {
        val previous = pointers[pointerId]
        if (previous == button) return
        pointers.remove(pointerId)
        if (previous != null && previous !in pointers.values) emit(previous, false)
        if (button != null) {
            val alreadyPressed = button in pointers.values
            pointers[pointerId] = button
            if (!alreadyPressed) emit(button, true)
        }
    }

    fun release(pointerId: Int, emit: (Int, Boolean) -> Unit) = update(pointerId, null, emit)

    fun cancel(emit: (Int, Boolean) -> Unit) {
        val pressed = pointers.values.toSet()
        pointers.clear()
        pressed.forEach { emit(it, false) }
    }
}
