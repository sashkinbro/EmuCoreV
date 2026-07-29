package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Test

class FrameLimitTest {
    @Test
    fun acceptsOnlySupportedPresentationRates() {
        FrameLimit.supportedValues.forEach { value ->
            assertEquals(value, FrameLimit.normalize(value))
        }
        listOf(-1, 1, 29, 31, 44, 46, 120).forEach { value ->
            assertEquals(FrameLimit.UNLIMITED, FrameLimit.normalize(value))
        }
    }
}
