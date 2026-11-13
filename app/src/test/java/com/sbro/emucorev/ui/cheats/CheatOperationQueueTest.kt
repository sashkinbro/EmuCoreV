package com.sbro.emucorev.ui.cheats

import com.sbro.emucorev.core.CheatOperationQueue
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.cancel
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class CheatOperationQueueTest {
    @Test fun oldSnapshotCannotToggleTheSameIndexInAReplacementPack() = runBlocking {
        var enabled = false
        val currentPack = "new-pack"
        assertNull(CheatOperationQueue.mutate("old-pack", { currentPack }) { enabled = true; true })
        assertFalse(enabled)
    }

    @Test fun matchingSnapshotCanToggleItsOwnPack() = runBlocking {
        var enabled = false
        assertEquals(true, CheatOperationQueue.mutate("same-pack", { "same-pack" }) { enabled = true; true })
        assertTrue(enabled)
    }

    @Test fun queuedReplacementCannotOvertakeABlockingIndexMutation() = runBlocking {
        val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val order = mutableListOf<String>()
        try {
            val toggle = scope.async {
                CheatOperationQueue.mutate("old", { "old" }) {
                    entered.countDown()
                    check(release.await(5, TimeUnit.SECONDS))
                    order.add("toggle")
                }
            }
            assertTrue(entered.await(5, TimeUnit.SECONDS))
            val replacement = scope.async {
                withContext(CheatOperationQueue.dispatcher) { order.add("replace") }
            }
            Thread.sleep(50)
            assertFalse(replacement.isCompleted)
            release.countDown()
            toggle.await()
            replacement.await()
            assertEquals(listOf("toggle", "replace"), order)
        } finally { release.countDown(); scope.cancel() }
    }
}
