package com.sbro.emucorev.core.vita

import com.sbro.emucorev.core.vita.GameDisplayModePolicy.Mode
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class GameDisplayModePolicyTest {
    private fun mode(id: Int, hz: Float, w: Int = 2400, h: Int = 1080) = Mode(id, w, h, hz)

    @Test
    fun multiplesOfSixtyAreRecognisedWithPanelRounding() {
        assertTrue(GameDisplayModePolicy.isMultipleOfGuestRate(60f))
        assertTrue(GameDisplayModePolicy.isMultipleOfGuestRate(59.99f))
        assertTrue(GameDisplayModePolicy.isMultipleOfGuestRate(120.0001f))
        assertTrue(GameDisplayModePolicy.isMultipleOfGuestRate(119.88f))
        assertFalse(GameDisplayModePolicy.isMultipleOfGuestRate(90f))
        assertFalse(GameDisplayModePolicy.isMultipleOfGuestRate(144f))
        assertFalse(GameDisplayModePolicy.isMultipleOfGuestRate(48f))
        assertFalse(GameDisplayModePolicy.isMultipleOfGuestRate(0f))
    }

    @Test
    fun panelAlreadyOnAMultipleIsLeftAlone() {
        val modes = listOf(mode(1, 60f), mode(2, 120f))
        assertNull(GameDisplayModePolicy.pick(mode(2, 120f), modes))
        assertNull(GameDisplayModePolicy.pick(mode(1, 60f), modes))
    }

    @Test
    fun ninetyHertzPanelWithSixtyModeSwitchesToSixty() {
        val modes = listOf(mode(1, 60f), mode(2, 90f), mode(3, 120f))
        assertEquals(1, GameDisplayModePolicy.pick(mode(2, 90f), modes)?.id)
    }

    @Test
    fun hundredFortyFourHertzPanelPrefersLowestMultiple() {
        val modes = listOf(mode(1, 60f), mode(2, 120f), mode(3, 144f))
        assertEquals(1, GameDisplayModePolicy.pick(mode(3, 144f), modes)?.id)
        assertEquals(2, GameDisplayModePolicy.pick(mode(3, 144f), modes.filter { it.id != 1 })?.id)
    }

    @Test
    fun panelWithoutAMultipleKeepsItsRate() {
        assertNull(GameDisplayModePolicy.pick(mode(1, 90f), listOf(mode(1, 90f), mode(2, 144f))))
        assertNull(GameDisplayModePolicy.pick(mode(1, 144f), listOf(mode(1, 144f))))
    }

    @Test
    fun modeOfAnotherResolutionIsNeverPicked() {
        val modes = listOf(mode(1, 90f), mode(2, 60f, w = 1920, h = 864), mode(3, 120f, w = 1920, h = 864))
        assertNull(GameDisplayModePolicy.pick(mode(1, 90f), modes))
    }
}
