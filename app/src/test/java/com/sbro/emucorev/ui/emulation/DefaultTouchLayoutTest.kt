package com.sbro.emucorev.ui.emulation

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class DefaultTouchLayoutTest {
    private fun layout(width: Float, height: Float, density: Float = 1f) = buildDefaultTouchLayout(
        width, height, width > height, 0.9f, density, 16f * density, 22f * density, 22f * density
    ).associateBy { it.id }

    @Test fun landscapeUsesEmuCoreXPrimaryRowWithShouldersImmediatelyAboveIt() {
        val controls = layout(800f, 400f)
        val left = controls.getValue(TouchControlIds.LEFT_STICK)
        val right = controls.getValue(TouchControlIds.RIGHT_STICK)
        val triangle = controls.getValue(TouchControlIds.TRIANGLE)
        val cross = controls.getValue(TouchControlIds.CROSS)
        val l1 = controls.getValue(TouchControlIds.L1)
        assertTrue(left.x < right.x)
        assertTrue(right.x + right.width < triangle.x)
        assertEquals((triangle.y + cross.y + cross.height) / 2f, left.y + left.height / 2f, 0.02f)
        assertTrue(l1.y + l1.height < triangle.y)
        assertTrue((triangle.y - l1.y - l1.height) * 400f < 40f)
    }

    @Test fun responsivePortraitAndTabletDefaultsStayInsideCanvasWithoutPrimaryButtonOverlap() {
        for ((width, height, density) in listOf(Triple(360f, 640f, 1f), Triple(3200f, 2000f, 2.5f), Triple(640f, 320f, 1f))) {
            val controls = layout(width, height, density)
            controls.values.forEach {
                assertTrue(it.id, it.x >= 0 && it.y >= 0 && it.x + it.width <= 1.001f && it.y + it.height <= 1.001f)
            }
            val left = controls.getValue(TouchControlIds.LEFT_STICK)
            val right = controls.getValue(TouchControlIds.RIGHT_STICK)
            assertTrue(left.x + left.width < right.x || left.y + left.height < right.y || right.y + right.height < left.y)
        }
    }

    @Test fun dpadTouchRectanglesHaveGapsInBothOrientations() {
        for ((width, height) in listOf(800f to 400f, 360f to 640f)) {
            val dpad = layout(width, height).values.filter { it.id.startsWith("dpad_") }
            for (first in dpad) for (second in dpad.filter { it.id != first.id }) {
                assertTrue("${first.id} overlaps ${second.id}",
                    first.x + first.width < second.x || second.x + second.width < first.x ||
                        first.y + first.height < second.y || second.y + second.height < first.y)
            }
        }
    }
}
