package com.sbro.emucorev.ui.emulation

import org.junit.Assert.assertEquals
import org.junit.Test

class TouchGridSnappingTest {
    @Test fun slowDragsAccumulateAndSnapAbsoluteCoordinates() {
        val residuals = mutableMapOf<String, Pair<Float, Float>>()
        var x = 24f
        repeat(5) { x += snapTouchDrag(residuals, "button", x, 24f, 2f to 0f, 24f, true).first }
        assertEquals(24f, x, 0.001f)
        repeat(3) { x += snapTouchDrag(residuals, "button", x, 24f, 2f to 0f, 24f, true).first }
        assertEquals(48f, x, 0.001f)
        val delta = snapTouchDrag(mutableMapOf(), "other", 37f, 14f, 1f to 0f, 24f, true)
        assertEquals(48f, 37f + delta.first, 0.001f)
        assertEquals(24f, 14f + delta.second, 0.001f)
    }

    @Test fun freeMovementPreservesThePointerDeltaAndIndependentControlsHaveIndependentRemainders() {
        val residuals = mutableMapOf<String, Pair<Float, Float>>()
        snapTouchDrag(residuals, "first", 24f, 24f, 10f to 0f, 24f, true)
        assertEquals(0f, snapTouchDrag(residuals, "second", 24f, 24f, 4f to 0f, 24f, true).first, 0f)
        assertEquals(3f to -5f, snapTouchDrag(residuals, "first", 24f, 24f, 3f to -5f, 24f, false))
    }
}
