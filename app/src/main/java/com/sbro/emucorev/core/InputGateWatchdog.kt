package com.sbro.emucorev.core

/**
 * Bounds how long input stays swallowed while a launch save state loads or its first frame is
 * awaited. The caller polls [check] with the current gate state; it reports a timeout once per
 * blocking period so the gate can be cleared. The timeout measures lack of progress: [progress]
 * restarts the clock, and time spent while [setPaused] is true (activity not resumed) is not counted.
 */
internal class InputGateWatchdog(
    private val timeoutMs: Long = TIMEOUT_MS,
    private val clock: () -> Long
) {
    private var blockedSince = NOT_BLOCKED
    private var expired = false
    private var paused = false

    /** Returns true exactly once when the gate has been blocking for [timeoutMs]. */
    @Synchronized
    fun check(blocking: Boolean): Boolean {
        if (!blocking) {
            blockedSince = NOT_BLOCKED
            expired = false
            return false
        }
        if (paused) return false
        val now = clock()
        if (blockedSince == NOT_BLOCKED) blockedSince = now
        if (expired || now - blockedSince < timeoutMs) return false
        expired = true
        return true
    }

    /** The load made visible progress: the no-progress clock restarts. */
    @Synchronized
    fun progress() {
        if (blockedSince != NOT_BLOCKED) blockedSince = clock()
        expired = false
    }

    /** While paused (activity in the background) nothing is counted; resuming restarts the clock. */
    @Synchronized
    fun setPaused(value: Boolean) {
        if (paused == value) return
        paused = value
        if (!value && blockedSince != NOT_BLOCKED) blockedSince = clock()
    }

    companion object {
        const val TIMEOUT_MS = 15_000L
        private const val NOT_BLOCKED = Long.MIN_VALUE
    }
}
