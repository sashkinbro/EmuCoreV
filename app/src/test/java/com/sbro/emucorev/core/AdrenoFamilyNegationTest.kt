package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Test

class AdrenoFamilyNegationTest {
    @Test
    fun negatedFamilyIsNotSupported() {
        assertEquals(
            setOf(AdrenoFamily.A6XX, AdrenoFamily.A7XX),
            GpuDriverRecommendations.supportedFamilies("Adreno 6xx/7xx (including Adreno 610 and 613); not Adreno 8xx")
        )
    }

    @Test
    fun plainFamiliesStillParsed() {
        assertEquals(
            setOf(AdrenoFamily.A6XX, AdrenoFamily.A7XX, AdrenoFamily.A8XX),
            GpuDriverRecommendations.supportedFamilies("Adreno 6xx/7xx/8xx; model support varies by release")
        )
        assertEquals(setOf(AdrenoFamily.A8XX), GpuDriverRecommendations.supportedFamilies("Adreno 8xx (830/840)"))
    }

    @Test
    fun negatedListIsDropped() {
        assertEquals(
            setOf(AdrenoFamily.A7XX),
            GpuDriverRecommendations.supportedFamilies("Adreno 7xx only, not 6xx or 8xx")
        )
    }
}
