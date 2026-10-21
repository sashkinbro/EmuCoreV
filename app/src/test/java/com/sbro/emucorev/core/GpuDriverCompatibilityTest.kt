package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class GpuDriverCompatibilityTest {
    @Test
    fun `maps current flagship Snapdragon generations to their Adreno families`() {
        assertEquals("Adreno 840", GpuDriverRecommendations.profileForSoc("Snapdragon 8 Elite Gen 5")?.adrenoName)
        assertEquals(AdrenoFamily.A8XX, GpuDriverRecommendations.profileForSoc("Snapdragon 8 Elite")?.family)
        assertEquals("Adreno 750", GpuDriverRecommendations.profileForSoc("Snapdragon 8 Gen 3")?.adrenoName)
        assertEquals(AdrenoFamily.A6XX, GpuDriverRecommendations.profileForSoc("Snapdragon 865 series")?.family)
        assertEquals(AdrenoFamily.A8XX, GpuDriverRecommendations.profileForSoc("Snapdragon 6 Gen 4")?.family)
        assertEquals(AdrenoFamily.A7XX, GpuDriverRecommendations.profileForSoc("Snapdragon 6 Gen 1")?.family)
        assertEquals("Adreno 810", GpuDriverRecommendations.profileForSoc("Snapdragon 7s Gen 4")?.adrenoName)
        assertEquals(AdrenoFamily.A8XX, GpuDriverRecommendations.profileForSoc("Snapdragon 7s Gen 3")?.family)
        assertEquals("Adreno 845", GpuDriverRecommendations.profileForSoc("Snapdragon 8 Elite Gen 6")?.adrenoName)
        assertEquals("Adreno 850", GpuDriverRecommendations.profileForSoc("Snapdragon 8 Elite Extreme Gen 6")?.adrenoName)
    }

    @Test
    fun `prefers the catalog mapping over a stale renderer string`() {
        val staleRenderer = GpuModelInfo(GpuVendor.ADRENO, "Adreno 710")
        assertEquals(
            "Adreno 810",
            GpuDriverRecommendations.profileForGpu("Snapdragon 7s Gen 3", staleRenderer)?.adrenoName
        )
    }

    @Test
    fun `falls back to the detected GPU when the SoC name is unknown`() {
        val detected = GpuModelInfo(GpuVendor.ADRENO, "Adreno 810")
        assertEquals(
            "Adreno 810",
            GpuDriverRecommendations.profileForGpu("Unknown SoC", detected)?.adrenoName
        )
        assertEquals(
            AdrenoFamily.A8XX,
            GpuDriverRecommendations.profileForGpu("Unknown SoC", detected)?.family
        )
        assertEquals(null, GpuDriverRecommendations.profileForGpu("Unknown SoC", null))
    }

    @Test
    fun `maps budget Snapdragon models exactly instead of a broad family token`() {
        assertEquals("Adreno 619", GpuDriverRecommendations.profileForSoc("Snapdragon 695")?.adrenoName)
        assertEquals("Adreno 619L", GpuDriverRecommendations.profileForSoc("Snapdragon 690")?.adrenoName)
        assertEquals("Adreno 610", GpuDriverRecommendations.profileForSoc("Snapdragon 680 series")?.adrenoName)
        assertEquals("Adreno 610", GpuDriverRecommendations.profileForSoc("Snapdragon 665")?.adrenoName)
        assertEquals("Adreno 613", GpuDriverRecommendations.profileForSoc("Snapdragon 4 Gen 2")?.adrenoName)
        assertEquals("Adreno 642L", GpuDriverRecommendations.profileForSoc("Snapdragon 778G series")?.adrenoName)
    }

    @Test
    fun `does not produce a recommendation for a non Snapdragon device`() {
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
        assertTrue(GpuDriverCompatibility.supportsAdrenoToolsCustomDrivers("sm7635-ac volcano"))
        assertTrue(GpuDriverCompatibility.supportsAdrenoToolsCustomDrivers("SM4450"))
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
