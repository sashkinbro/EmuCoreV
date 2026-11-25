package com.sbro.emucorev.data

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class ProfileCoroutinesTest {
    @Test fun runCatchingCancellableRethrowsCancellation() {
        try {
            runCatchingCancellable { throw CancellationException("cancelled") }
            fail("cancellation must propagate")
        } catch (expected: CancellationException) {
            assertEquals("cancelled", expected.message)
        }
    }

    @Test fun runCatchingCancellableCapturesOtherFailures() {
        val result = runCatchingCancellable { error("boom") }
        assertTrue(result.isFailure)
        assertEquals("boom", result.exceptionOrNull()?.message)
        assertEquals(7, runCatchingCancellable { 7 }.getOrNull())
    }

    @Test fun collectReportingSurfacesAClosedListenerError() = runBlocking {
        // A snapshot listener that close(error)s used to end the collector silently (B061).
        val failing = callbackFlow<Int> {
            trySend(1)
            close(IllegalStateException("listener failed"))
            awaitClose { }
        }
        val received = mutableListOf<Int>()
        var reported: Throwable? = null
        failing.collectReporting(onError = { reported = it }) { received += it }
        assertEquals(listOf(1), received)
        assertEquals("listener failed", reported?.message)
    }

    @Test fun collectReportingDoesNotReportCompletion() = runBlocking {
        var reported = false
        val received = mutableListOf<Int>()
        flowOf(1, 2).collectReporting(onError = { reported = true }) { received += it }
        assertEquals(listOf(1, 2), received)
        assertTrue(!reported)
    }
}
