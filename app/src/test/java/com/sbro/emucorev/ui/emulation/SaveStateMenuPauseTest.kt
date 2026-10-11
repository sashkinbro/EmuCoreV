package com.sbro.emucorev.ui.emulation

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Test

class SaveStateMenuPauseTest {
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
