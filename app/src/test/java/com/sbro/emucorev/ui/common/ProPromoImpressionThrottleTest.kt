package com.sbro.emucorev.ui.common

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ProPromoImpressionThrottleTest {
    private val minute = 60_000L

    @Test
    fun logsOncePerPlacementPerInterval() {
        val throttle = ProPromoImpressionThrottle(10 * minute)
        assertTrue(throttle.shouldLog("library", 1_000L))
        assertFalse(throttle.shouldLog("library", 1_000L + 9 * minute))
        assertTrue(throttle.shouldLog("library", 1_000L + 10 * minute))
    }

    @Test
    fun placementsAreIndependent() {
        val throttle = ProPromoImpressionThrottle(10 * minute)
        assertTrue(throttle.shouldLog("library", 5_000L))
        assertTrue(throttle.shouldLog("themes", 5_001L))
        assertFalse(throttle.shouldLog("themes", 5_002L))
    }
}
