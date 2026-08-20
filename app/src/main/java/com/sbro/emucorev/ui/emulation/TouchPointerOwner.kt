package com.sbro.emucorev.ui.emulation

/** Owns one Android pointer for the lifetime of a touch-control press. */
internal class TouchPointerOwner {
    private var pointerId: Int = NO_POINTER

    val hasOwner: Boolean
        get() = pointerId != NO_POINTER

    fun acquire(candidatePointerId: Int): Boolean {
        if (pointerId == NO_POINTER) {
            pointerId = candidatePointerId
        }
        return pointerId == candidatePointerId
    }

    fun release(candidatePointerId: Int): Boolean {
        if (pointerId != candidatePointerId) return false
        pointerId = NO_POINTER
        return true
    }

    fun cancel(): Boolean {
        if (pointerId == NO_POINTER) return false
        pointerId = NO_POINTER
        return true
    }

    private companion object {
        const val NO_POINTER = -1
    }
}
