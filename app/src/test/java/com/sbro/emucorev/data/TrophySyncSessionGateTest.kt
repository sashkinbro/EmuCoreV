package com.sbro.emucorev.data

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.yield
import org.junit.Assert.*
import org.junit.Test

class TrophySyncSessionGateTest {
    @Test fun gameStartupAndSyncNeverOverlap() = runBlocking {
        val gate = TrophySyncSessionGate()
        val entered = CompletableDeferred<Unit>()
        val release = CompletableDeferred<Unit>()
        val sync = async { gate.whileStopped { entered.complete(Unit); release.await() } }
        entered.await()
        val start = async { gate.gameStarting() }
        yield()
        assertFalse(start.isCompleted)
        release.complete(Unit)
        sync.await(); start.await()
        var touched = false
        try { gate.whileStopped { touched = true }; fail("sync admitted during gameplay") }
        catch (_: IllegalStateException) { }
        assertFalse(touched)
        gate.gameStopped()
        gate.whileStopped { touched = true }
        assertTrue(touched)
    }
}
