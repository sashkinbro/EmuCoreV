package com.sbro.emucorev.core

import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class LiveResolutionBoundaryTest {
    @Test
    fun chipShowsTheLiveValueWhenTheSessionReportsASelectableOne() {
        // profile says 1x, but the session renders 2x (for example after a state load)
        assertEquals(2f, LiveGraphicsSettings.displayedResolution(1f, 2f, 4f))
        // live value unknown or not selectable here: fall back to the profile
        assertEquals(1f, LiveGraphicsSettings.displayedResolution(1f, 0f, 4f))
        assertEquals(1f, LiveGraphicsSettings.displayedResolution(1f, 2.25f, 4f))
        assertEquals(1f, LiveGraphicsSettings.displayedResolution(1f, 6f, 4f))
    }

    @Test
    fun appliesImmediatelyAtAFrameBoundary() = runBlocking {
        var frames = 0
        val ok = applyResolutionAtFrameBoundary(8, { APPLY_OK }, { frames++ })
        assertTrue(ok)
        assertEquals(0, frames)
    }

    @Test
    fun letsTheGuestFinishAnOpenSceneThenApplies() = runBlocking {
        var calls = 0
        var frames = 0
        val midSceneFlags = mutableListOf<Boolean>()
        val ok = applyResolutionAtFrameBoundary(8, { allowMidScene ->
            midSceneFlags += allowMidScene
            if (++calls < 3) APPLY_SCENE_OPEN else APPLY_OK
        }, { frames++ })
        assertTrue(ok)
        assertEquals(2, frames)
        assertTrue(midSceneFlags.none { it })
    }

    @Test
    fun fallsBackToMidSceneOnlyOnTheLastAttempt() = runBlocking {
        val flags = mutableListOf<Boolean>()
        val ok = applyResolutionAtFrameBoundary(4, { allowMidScene ->
            flags += allowMidScene
            if (allowMidScene) APPLY_OK else APPLY_SCENE_OPEN
        }, {})
        assertTrue(ok)
        assertEquals(listOf(false, false, false, true), flags)
    }

    @Test
    fun aRealFailureStopsAtOnce() = runBlocking {
        var calls = 0
        val ok = applyResolutionAtFrameBoundary(8, { calls++; 0 }, {})
        assertFalse(ok)
        assertEquals(1, calls)
    }
}
