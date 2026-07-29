package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Test

class MobileSocNameMapperTest {
    @Test
    fun resolvesCurrentFlagshipPlatformCodes() {
        assertEquals("Snapdragon 8 Elite Gen 5", MobileSocNameMapper.resolve("SM8850-AC"))
        assertEquals("Dimensity 9500", MobileSocNameMapper.resolve("MT6993"))
        assertEquals("Exynos 2600", MobileSocNameMapper.resolve("s5e9965"))
        assertEquals("Google Tensor G5", MobileSocNameMapper.resolve("laguna"))
    }

    @Test
    fun resolvesOlderAndAlternativeVendorCodes() {
        assertEquals("Snapdragon 835", MobileSocNameMapper.resolve("MSM8998"))
        assertEquals("Snapdragon 845", MobileSocNameMapper.resolve("SDM845"))
        assertEquals("Helio G99/G100", MobileSocNameMapper.resolve("mt6789v/cd"))
        assertEquals("Dimensity 1100", MobileSocNameMapper.resolve("MT6891Z/CZA"))
        assertEquals("Kirin 970", MobileSocNameMapper.resolve("hi3670"))
        assertEquals("Unisoc T618", MobileSocNameMapper.resolve("T618"))
        assertEquals("Rockchip RK3588", MobileSocNameMapper.resolve("rk3588s"))
    }

    @Test
    fun resolvesAndroidHandheldSpecificPlatforms() {
        assertEquals("Snapdragon G3x Gen 1", MobileSocNameMapper.resolve("QTI SG8175P"))
        assertEquals("Snapdragon 8 Gen 2", MobileSocNameMapper.resolve("QCS8550"))
        assertEquals("Snapdragon 8 Gen 2", MobileSocNameMapper.resolve(null, board = "kalama"))
        assertEquals("Helio G99", MobileSocNameMapper.resolve("MT8781V/CA"))
    }

    @Test
    fun usesHandheldModelToDisambiguateRebrandedPhoneSilicon() {
        assertEquals(
            "Snapdragon G3x Gen 2",
            MobileSocNameMapper.resolve("SM8550", model = "AYANEO Pocket S")
        )
        assertEquals(
            "Snapdragon G2 Gen 2",
            MobileSocNameMapper.resolve("SM7675", model = "Retroid Pocket G2")
        )
        assertEquals(
            "Snapdragon G3 Gen 3",
            MobileSocNameMapper.resolve("SM8650", model = "AYANEO Pocket S2")
        )
        assertEquals(
            "Snapdragon 865 series",
            MobileSocNameMapper.resolve("unknown", model = "AYANEO Pocket Micro 2")
        )
        assertEquals("Snapdragon 8 Gen 2", MobileSocNameMapper.resolve("SM8550", model = "Phone"))
    }

    @Test
    fun neverShowsAnUnmappedRawPlatformCode() {
        assertEquals("Snapdragon", MobileSocNameMapper.resolve("SM9999", manufacturer = "Qualcomm"))
        assertEquals("MediaTek", MobileSocNameMapper.resolve("MT9999"))
        assertEquals("Unknown SoC", MobileSocNameMapper.resolve("future_platform"))
    }
}
