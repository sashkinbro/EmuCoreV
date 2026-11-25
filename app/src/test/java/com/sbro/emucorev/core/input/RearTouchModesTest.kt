package com.sbro.emucorev.core.input

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class RearTouchModesTest {
    private data class Event(val slot: Int, val pressed: Boolean, val x: Float, val y: Float)

    private class Harness(trigger: RearTriggerMode, mode: RearPositionMode = RearPositionMode.FIXED,
        zone: RearZone = RearZone.FULL) {
        val events = mutableListOf<Event>()
        val scheduled = mutableListOf<Pair<Long, () -> Unit>>()
        val dispatcher = RearTouchDispatcher({ slot, pressed, x, y -> events += Event(slot, pressed, x, y) },
            { delay, task -> scheduled += delay to task })

        init {
            dispatcher.configure(RearTouchProfile(listOf(RearTouchPoint(0, 0.25f, 0.35f, trigger = trigger, positionMode = mode)), zone))
            dispatcher.setActive(true)
        }
    }

    @Test fun holdKeepsTheContactOnlyWhileASourceIsHeld() {
        val h = Harness(RearTriggerMode.HOLD)
        h.dispatcher.setSource(0, "a", true)
        h.dispatcher.setSource(0, "b", true)
        h.dispatcher.setSource(0, "a", false)
        assertTrue(h.dispatcher.isHeld(0))
        h.dispatcher.setSource(0, "b", false)
        assertFalse(h.dispatcher.isHeld(0))
        assertEquals(listOf(true, false), h.events.map { it.pressed })
    }

    @Test fun toggleLatchesOnPressAndReleasesOnTheNextPress() {
        val h = Harness(RearTriggerMode.TOGGLE)
        h.dispatcher.setSource(0, "a", true)   // press: contact down
        h.dispatcher.setSource(0, "a", false)  // release keeps the contact
        assertTrue(h.dispatcher.isHeld(0))
        assertEquals(listOf(true), h.events.map { it.pressed })
        h.dispatcher.setSource(0, "a", true)   // second press lifts
        h.dispatcher.setSource(0, "a", false)
        assertFalse(h.dispatcher.isHeld(0))
        assertEquals(listOf(true, false), h.events.map { it.pressed })
    }

    @Test fun toggleIgnoresRepeatedDownsOfTheSameSource() {
        val h = Harness(RearTriggerMode.TOGGLE)
        repeat(5) { h.dispatcher.setSource(0, "axis", true) } // an analogue trigger reports on every motion event
        assertEquals(listOf(true), h.events.map { it.pressed })
    }

    @Test fun pauseOrDeactivationClearsALatchedContact() {
        val h = Harness(RearTriggerMode.TOGGLE)
        h.dispatcher.setSource(0, "a", true)
        h.dispatcher.setActive(false)
        assertFalse(h.dispatcher.isHeld(0))
        assertEquals(listOf(true, false), h.events.map { it.pressed })
    }

    @Test fun tapMakesAShortContactReleasedByTheScheduler() {
        val h = Harness(RearTriggerMode.TAP)
        h.dispatcher.setSource(0, "a", true)
        h.dispatcher.setSource(0, "a", false) // lifting the finger early must not cut the tap short
        assertTrue(h.dispatcher.isHeld(0))
        assertEquals(1, h.scheduled.size)
        assertEquals(REAR_TAP_MILLIS, h.scheduled[0].first)
        h.scheduled[0].second()
        assertFalse(h.dispatcher.isHeld(0))
        assertEquals(listOf(true, false), h.events.map { it.pressed })
    }

    @Test fun aSecondTapExtendsTheContactInsteadOfEndingItEarly() {
        val h = Harness(RearTriggerMode.TAP)
        h.dispatcher.setSource(0, "a", true)
        h.dispatcher.setSource(0, "a", false)
        h.dispatcher.setSource(0, "a", true)   // before the first release fires
        h.scheduled[0].second()                // stale release: ignored
        assertTrue(h.dispatcher.isHeld(0))
        h.scheduled[1].second()
        assertFalse(h.dispatcher.isHeld(0))
    }

    @Test fun resetCancelsAPendingTapRelease() {
        val h = Harness(RearTriggerMode.TAP)
        h.dispatcher.setSource(0, "a", true)
        h.dispatcher.reset()
        h.dispatcher.setSource(0, "b", true) // new tap after the reset
        h.scheduled[0].second()               // old release must not lift the new tap
        assertTrue(h.dispatcher.isHeld(0))
    }

    @Test fun zoneMapsTheScreenRectangleOntoTheWholePanel() {
        val zone = RearZone(0.5f, 0f, 1f, 1f)
        assertEquals(0f to 0.5f, zone.map(0.5f, 0.5f))
        assertEquals(1f to 1f, zone.map(1f, 1f))
        assertEquals(0f to 0f, zone.map(0.1f, 0f)) // outside clamps to the panel edge
        assertEquals(.5f to .5f, RearZone(.25f, .25f, .75f, .75f).map(.5f, .5f))
    }

    @Test fun zoneModeFollowsThePointerThroughTheZone() {
        val h = Harness(RearTriggerMode.HOLD, RearPositionMode.ZONE, RearZone(0f, 0f, .5f, .5f))
        h.dispatcher.move(RearPositionMode.ZONE, .25f, .25f)
        h.dispatcher.setSource(0, "a", true)
        assertEquals(.5f to .5f, h.events.last().x to h.events.last().y)
        h.dispatcher.move(RearPositionMode.ZONE, .5f, 0f)
        assertEquals(1f to 0f, h.events.last().x to h.events.last().y)
        // POINTER-mode moves do not drive a ZONE point
        val before = h.events.size
        h.dispatcher.move(RearPositionMode.POINTER, .1f, .1f)
        assertEquals(before, h.events.size)
    }

    @Test fun degenerateZonesAreWidenedToAUsableSize() {
        val zone = RearZone(.5f, .5f, .5f, .5f).normalized()
        assertTrue(zone.right - zone.left >= RearZone.MIN_SIZE)
        assertTrue(zone.bottom - zone.top >= RearZone.MIN_SIZE)
        val dragged = rearZoneFromDrag(.8f, .9f, .2f, .1f)
        assertEquals(RearZone(.2f, .1f, .8f, .9f), dragged)
    }

    @Test fun presetsAreRecognisedAndDistinct() {
        RearZonePreset.entries.forEach { assertEquals(it, RearZonePreset.match(it.zone)) }
        assertNotEquals(RearZonePreset.LEFT_HALF.zone, RearZonePreset.RIGHT_HALF.zone)
        assertEquals(null, RearZonePreset.match(RearZone(.1f, .1f, .9f, .8f)))
    }

    @Test fun padAspectIsTheRearPanelAspect() {
        assertEquals(1919f / 890f, REAR_PANEL_WIDTH / REAR_PANEL_HEIGHT, 1e-6f)
    }

    @Test fun mouseButtonsMapToNamedIndexes() {
        assertEquals(listOf(0, 1, 2, 3, 4, -1), listOf(1, 2, 4, 8, 16, 32).map(::rearMouseButtonIndex))
    }
}
