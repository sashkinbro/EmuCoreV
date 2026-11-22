package com.sbro.emucorev.core

/**
 * Bounds how long input stays swallowed while a launch save state loads or its first frame is
 * awaited. The caller polls [check] with the current gate state; it reports a timeout once per
 * blocking period so the gate can be cleared and the load-error path shown.
 */
internal class InputGateWatchdog(
    private val timeoutMs: Long = TIMEOUT_MS,
    private val clock: () -> Long
) {
    private var blockedSince = NOT_BLOCKED
    private var expired = false

    /** Returns true exactly once when the gate has been blocking for [timeoutMs]. */
    @Synchronized
    fun check(blocking: Boolean): Boolean {
        if (!blocking) {
            blockedSince = NOT_BLOCKED
            expired = false
            return false
        }
        val now = clock()
        if (blockedSince == NOT_BLOCKED) blockedSince = now
        if (expired || now - blockedSince < timeoutMs) return false
        expired = true
        return true
    }

    companion object {
        const val TIMEOUT_MS = 15_000L
        private const val NOT_BLOCKED = Long.MIN_VALUE
    }
}
