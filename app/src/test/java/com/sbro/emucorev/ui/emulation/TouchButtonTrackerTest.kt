package com.sbro.emucorev.ui.emulation

import org.junit.Assert.assertEquals
import org.junit.Test

class TouchButtonTrackerTest {
    private val events = mutableListOf<Pair<Int, Boolean>>()
    private val emit: (Int, Boolean) -> Unit = { button, pressed -> events += button to pressed }

    @Test fun liftingAnotherFingerDoesNotReleaseAHeldButton() {
        val tracker = TouchButtonTracker()
        tracker.update(10, 1, emit)
        tracker.update(20, 1, emit)
        tracker.release(10, emit)
        tracker.release(99, emit)
        assertEquals(listOf(1 to true), events)
        tracker.release(20, emit)
        assertEquals(listOf(1 to true, 1 to false), events)
    }

    @Test fun slidingBetweenButtonsReleasesOldButtonBeforePressingNewOne() {
        val tracker = TouchButtonTracker()
        tracker.update(7, 1, emit)
        tracker.update(7, 1, emit)
        tracker.update(7, 2, emit)
        tracker.update(7, null, emit)
        assertEquals(listOf(1 to true, 1 to false, 2 to true, 2 to false), events)
    }

    @Test fun cancelOrLayoutReplacementReleasesEveryButtonExactlyOnce() {
        val tracker = TouchButtonTracker()
        tracker.update(3, 1, emit)
        tracker.update(5, 2, emit)
        tracker.update(7, 2, emit)
        tracker.cancel(emit)
        tracker.cancel(emit)
        assertEquals(listOf(1 to true, 2 to true, 1 to false, 2 to false), events)
        tracker.update(3, 1, emit)
        tracker.release(3, emit)
        assertEquals(listOf(1 to true, 1 to false), events.takeLast(2))
    }
}
