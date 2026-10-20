package com.sbro.emucorev.ui.emulation

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Test

class SaveStateMenuPauseTest {
    @Test
    fun cancellationWhileEnteringPauseStillQueuesResume() = runBlocking {
        val pauses = mutableListOf<Boolean>()
        val entered = CompletableDeferred<Unit>()
        val job = launch {
            withSaveStateMenuPause({ paused ->
                pauses += paused
                if (paused) {
                    entered.complete(Unit)
                    awaitCancellation()
                }
            }, { false }) { error("Must not start before pause completes") }
        }
        entered.await()
        job.cancel()
        job.join()
        assertEquals(listOf(true, false), pauses)
    }

    @Test
    fun immediateFailureReleasesPauseWithoutRecomposition() = runBlocking {
        val pauses = mutableListOf<Boolean>()
        try {
            withSaveStateMenuPause(pauses::add, { false }) {
                error("preflight failure")
            }
        } catch (_: IllegalStateException) { }
        assertEquals(listOf(true, false), pauses)
    }

    @Test
    fun cancellationReleasesPauseWithoutRecomposition() = runBlocking {
        val pauses = mutableListOf<Boolean>()
        try {
            withSaveStateMenuPause(pauses::add, { false }) {
                throw CancellationException("overlay disposed")
            }
        } catch (_: CancellationException) { }
        assertEquals(listOf(true, false), pauses)
    }

    @Test
    fun finalizationReadsRemainingPauseReasonAfterOperation() = runBlocking {
        val pauses = mutableListOf<Boolean>()
        var menuOpen = false
        withSaveStateMenuPause(pauses::add, { menuOpen }) {
            menuOpen = true
        }
        assertEquals(listOf(true, true), pauses)
    }
}
