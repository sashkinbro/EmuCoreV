package com.sbro.emucorev.core

import com.sbro.emucorev.data.drive.DriveBackupException
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.delay

/** Shares one exclusion boundary between VM startup and backup/restore filesystem changes. */
object BackupSessionGate {
    private val mutex = Mutex()
    @Volatile private var running = false
    @Volatile private var starting = false
    val gameBusy get() = running || starting

    suspend fun <T> whileStopped(block: suspend () -> T): T = mutex.withLock {
        checkpoint()
        block()
    }

    /** Background installers wait for gameplay and acquire the same startup boundary atomically. */
    suspend fun <T> awaitStopped(onWaiting: () -> Unit = {}, block: suspend () -> T): T {
        while (true) {
            mutex.lock()
            if (!gameBusy) {
                try { return block() }
                finally { mutex.unlock() }
            }
            mutex.unlock()
            onWaiting()
            delay(1000)
        }
    }

    suspend fun start(active: () -> Boolean, block: suspend () -> Boolean): Boolean {
        starting = true
        return try {
            mutex.withLock {
                running = true
                try { block().also { running = active() } }
                catch (error: Throwable) { running = active(); throw error }
            }
        } finally { starting = false }
    }

    fun stopped() { running = false }
    fun checkpoint() {
        if (gameBusy) throw DriveBackupException("busy")
    }
}
