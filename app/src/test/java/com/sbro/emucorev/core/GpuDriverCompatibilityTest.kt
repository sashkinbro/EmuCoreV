package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class GpuDriverCompatibilityTest {
    @Test
    fun `maps Snapdragon generations to their Adreno families`() {
        assertEquals("Adreno 840", GpuDriverRecommendations.profileForSoc("Snapdragon 8 Elite Gen 5")?.adrenoName)
        assertEquals(AdrenoFamily.A8XX, GpuDriverRecommendations.profileForSoc("Snapdragon 8 Elite")?.family)
        assertEquals("Adreno 750", GpuDriverRecommendations.profileForSoc("Snapdragon 8 Gen 3")?.adrenoName)
        assertEquals(AdrenoFamily.A6XX, GpuDriverRecommendations.profileForSoc("Snapdragon 865 series")?.family)
        assertEquals(AdrenoFamily.A8XX, GpuDriverRecommendations.profileForSoc("Snapdragon 6 Gen 4")?.family)
        assertEquals(AdrenoFamily.A7XX, GpuDriverRecommendations.profileForSoc("Snapdragon 6 Gen 1")?.family)
    }

    @Test
    fun `does not guess a phone driver family for unsupported devices`() {
        assertEquals(null, GpuDriverRecommendations.profileForSoc("Dimensity 9400"))
        assertEquals(null, GpuDriverRecommendations.profileForSoc("Snapdragon G3x Gen 2"))
    }

    @Test
    fun `matches broad and explicitly limited driver packages`() {
        val elite = GpuDriverRecommendations.profileForSoc("Snapdragon 8 Elite")
        val gen5 = GpuDriverRecommendations.profileForSoc("Snapdragon 8 Elite Gen 5")
        val broad = driver(gpu = "Adreno 6xx/7xx/8xx")
        val limited = driver(gpu = "Adreno 8xx (830/840)")
        val a7 = driver(gpu = "Adreno 6xx/7xx")

        assertEquals(GpuDriverMatch.COMPATIBLE, GpuDriverRecommendations.match(broad, elite))
        assertEquals(GpuDriverMatch.COMPATIBLE, GpuDriverRecommendations.match(limited, elite))
        assertEquals(GpuDriverMatch.COMPATIBLE, GpuDriverRecommendations.match(limited, gen5))
        assertEquals(GpuDriverMatch.OTHER_FAMILY, GpuDriverRecommendations.match(a7, elite))
        assertTrue(GpuDriverRecommendations.supportedFamilies(broad.gpu).contains(AdrenoFamily.A7XX))
    }

    @Test
    fun `enables custom drivers for Qualcomm platform identifiers`() {
        assertTrue(GpuDriverCompatibility.supportsAdrenoToolsCustomDrivers("qcom SM8250 kona"))
        assertTrue(GpuDriverCompatibility.supportsAdrenoToolsCustomDrivers("Qualcomm Snapdragon 865 Adreno"))
    }

    @Test
    fun `rejects custom drivers for known non Adreno identifiers`() {
        assertEquals(false, GpuDriverCompatibility.supportsAdrenoToolsCustomDrivers("MediaTek MT6899 Mali"))
        assertEquals(false, GpuDriverCompatibility.supportsAdrenoToolsCustomDrivers("Samsung Exynos 2400"))
        assertEquals(false, GpuDriverCompatibility.supportsAdrenoToolsCustomDrivers("Google Tensor G4"))
    }

    private fun driver(gpu: String) = RemoteGpuDriver(
        id = "test",
        name = "Test",
        variant = "Auto",
        gpu = gpu,
        description = "",
        recommended = false,
        downloadUrl = "https://example.com/test.zip",
        sourceUrl = "",
        credits = "",
        sizeBytes = null
    )
}
