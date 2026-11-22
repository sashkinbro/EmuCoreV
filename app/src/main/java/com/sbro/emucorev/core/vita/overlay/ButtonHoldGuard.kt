package com.sbro.emucorev.core.vita.overlay

/**
 * Keeps every on-screen button press visible to the game for at least [minHoldMs].
 *
 * Overlay buttons reach the game through an SDL virtual gamepad that the core samples once per
 * update; a press released before the next sample (a quick tap) was never seen by the game. A
 * release that comes too early is deferred until the minimum hold has elapsed; a new press of the
 * same button cancels the deferred release.
 */
internal class ButtonHoldGuard(
    private val minHoldMs: Long = MIN_HOLD_MS,
    private val clock: () -> Long,
    private val schedule: (delayMs: Long, action: () -> Unit) -> Unit,
    private val apply: (button: Int, pressed: Boolean) -> Unit
) {
    private val pressedAt = HashMap<Int, Long>()
    private val generation = HashMap<Int, Int>()

    @Synchronized
    fun send(button: Int, pressed: Boolean) {
        val gen = (generation[button] ?: 0) + 1
        generation[button] = gen
        if (pressed) {
            pressedAt[button] = clock()
            apply(button, true)
            return
        }
        val since = pressedAt[button]
        val remaining = if (since == null) 0L else minHoldMs - (clock() - since)
        if (remaining <= 0L) {
            pressedAt.remove(button)
            apply(button, false)
            return
        }
        schedule(remaining) { releaseIfCurrent(button, gen) }
    }

    @Synchronized
    private fun releaseIfCurrent(button: Int, gen: Int) {
        if (generation[button] != gen) return // pressed again meanwhile
        pressedAt.remove(button)
        apply(button, false)
    }

    companion object {
        /** Three frames at 60 Hz: survives one missed sample at 30 fps games too. */
        const val MIN_HOLD_MS = 50L
    }
}
