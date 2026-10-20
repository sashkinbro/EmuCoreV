package com.sbro.emucorev.data

import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/** Native lifetime, deliberately independent of Android onStop and play-time accounting. */
internal class TrophySyncSessionGate {
    private val mutex = Mutex()
    @Volatile private var busy = false

    val isBusy: Boolean get() = busy

    suspend fun gameStarting() {
        // Close admission immediately, then drain any already admitted sync off the UI thread.
        busy = true
        mutex.withLock { }
    }

    fun gameStopped() { busy = false }

    suspend fun <T> whileStopped(block: suspend () -> T): T = mutex.withLock {
        check(!busy) { "Game session is active" }
        block()
    }

    companion object {
        val shared = TrophySyncSessionGate()
    }
}
