package com.sbro.emucorev.ui.emulation

import org.junit.Assert.assertEquals
import org.junit.Test

class TouchActionTrackerTest {
    @Test fun overlappingComboAndDuplicateStayPressedUntilTheirLastOwnerReleases() {
        val events = mutableListOf<Pair<Int, Boolean>>()
        val tracker = TouchActionTracker { action, pressed -> events += action to pressed }
        tracker.press("combo", setOf(1, 2))
        tracker.press("duplicate", setOf(1))
        tracker.release("combo")
        assertEquals(listOf(1 to true, 2 to true, 2 to false), events)
        tracker.release("duplicate")
        assertEquals(1 to false, events.last())
    }

    @Test fun repeatedDownAndCancelNeverDuplicateEventsOrLeaveAnActionHeld() {
        val events = mutableListOf<Pair<Int, Boolean>>()
        val tracker = TouchActionTracker { action, pressed -> events += action to pressed }
        tracker.press("button", setOf(1, 2))
        tracker.press("button", setOf(1, 2))
        tracker.release("absent")
        tracker.cancel()
        tracker.cancel()
        assertEquals(listOf(1 to true, 2 to true, 1 to false, 2 to false), events)
    }

    @Test fun replacingAnOwnersActionsRetainsSharedActionsAndReleasesOldOnes() {
        val events = mutableListOf<Pair<Int, Boolean>>()
        val tracker = TouchActionTracker { action, pressed -> events += action to pressed }
        tracker.press("combo", setOf(1, 2))
        tracker.press("combo", setOf(2, 3))
        assertEquals(listOf(1 to true, 2 to true, 1 to false, 3 to true), events)
    }
}
