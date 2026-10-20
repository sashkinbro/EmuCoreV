package com.sbro.emucorev.core

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.*
import org.junit.Test

class MenuPauseQueueTest {
    @Test fun slowNativePauseDoesNotBlockCallerAndRequestsStayOrdered() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val applied = mutableListOf<Boolean>()
        val queue = MenuPauseQueue(scope) { paused ->
            if (applied.isEmpty()) {
                entered.countDown()
                assertTrue(release.await(5, TimeUnit.SECONDS))
            }
            applied += paused
        }
        try {
            val first = queue.request(true)
            assertTrue(entered.await(2, TimeUnit.SECONDS))
            val second = queue.request(false)
            val last = queue.request(true)
            assertFalse(first.isCompleted)
            assertFalse(second.isCompleted)
            assertFalse(last.isCompleted)
            release.countDown()
            withTimeout(2_000) { last.await() }
            assertEquals(listOf(true, false, true), applied)
        } finally {
            release.countDown()
            queue.close()
            scope.cancel()
        }
    }
}
