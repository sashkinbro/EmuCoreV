package com.sbro.emucorev.ui.settings

import org.junit.Assert.assertEquals
import org.junit.Test

class SettingsTabCenteringTest {
    @Test
    fun centeredItemDoesNotScroll() {
        assertEquals(0f, centeredTabScrollDelta(400, 200, 0, 1000), 0f)
    }

    @Test
    fun itemLeftOfCenterScrollsBackward() {
        assertEquals(-300f, centeredTabScrollDelta(100, 200, 0, 1000), 0f)
    }

    @Test
    fun itemRightOfCenterScrollsForward() {
        assertEquals(300f, centeredTabScrollDelta(700, 200, 0, 1000), 0f)
    }

    @Test
    fun viewportInsetsAreIncludedInCenter() {
        assertEquals(0f, centeredTabScrollDelta(450, 100, 100, 900), 0f)
    }
}
