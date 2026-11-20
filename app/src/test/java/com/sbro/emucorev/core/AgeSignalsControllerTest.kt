package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class AgeSignalsControllerTest {
    @Test
    fun emptySnapshotIsNotMinor() {
        assertFalse(AgeSignalsSnapshot.EMPTY.isMinor)
        assertFalse(AgeSignalsSnapshot.EMPTY.consentDeclined)
    }

    @Test
    fun upperBoundBelow18IsMinor() {
        assertTrue(AgeSignalsSnapshot(ageLower = 13, ageUpper = 15).isMinor)
        assertTrue(AgeSignalsSnapshot(ageLower = 16, ageUpper = 17).isMinor)
    }

    @Test
    fun adultOrOpenEndedRangeIsNotMinor() {
        assertFalse(AgeSignalsSnapshot(ageLower = 18, ageUpper = null).isMinor)
        assertFalse(AgeSignalsSnapshot(ageLower = 18, ageUpper = 18).isMinor)
    }

    @Test
    fun declinedStatusOnly() {
        assertTrue(AgeSignalsSnapshot(significantChangeStatus = 3).consentDeclined)
        assertFalse(AgeSignalsSnapshot(significantChangeStatus = 1).consentDeclined)
        assertFalse(AgeSignalsSnapshot(significantChangeStatus = 2).consentDeclined)
    }

    @Test
    fun transientErrorsRetryWithBackoffThenStop() {
        for (code in intArrayOf(-3, -5, -8)) {
            assertEquals(1_000L, AgeSignalsErrors.retryDelayMs(code, 0))
            assertEquals(2_000L, AgeSignalsErrors.retryDelayMs(code, 1))
            assertEquals(4_000L, AgeSignalsErrors.retryDelayMs(code, 2))
            assertNull(AgeSignalsErrors.retryDelayMs(code, 3))
        }
    }

    @Test
    fun finalErrorsNeverRetry() {
        for (code in intArrayOf(-1, -2, -4, -6, -7, -9, -10, -11, -100)) {
            assertNull(AgeSignalsErrors.retryDelayMs(code, 0))
        }
    }
}
