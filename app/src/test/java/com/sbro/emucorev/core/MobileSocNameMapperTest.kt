package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Test

class MobileSocNameMapperTest {
    @Test
    fun resolvesCurrentFlagshipPlatformCodes() {
        assertEquals("Snapdragon 8 Elite Extreme Gen 6", MobileSocNameMapper.resolve("SM8975"))
        assertEquals("Snapdragon 8 Elite Gen 6", MobileSocNameMapper.resolve("SM8950"))
        assertEquals("Snapdragon 8 Gen 6", MobileSocNameMapper.resolve("SM8845 Pro"))
        assertEquals("Snapdragon 8 Elite Gen 5", MobileSocNameMapper.resolve("SM8850-AC"))
        assertEquals("Dimensity 9600 Pro", MobileSocNameMapper.resolve("MT6995"))
        assertEquals("Dimensity 9500", MobileSocNameMapper.resolve("MT6993"))
        assertEquals("Dimensity 8500", MobileSocNameMapper.resolve("MT6899Z"))
        assertEquals("Exynos 2600", MobileSocNameMapper.resolve("s5e9965"))
        assertEquals("Exynos 1680", MobileSocNameMapper.resolve("S5E8865"))
        assertEquals("Google Tensor G6", MobileSocNameMapper.resolve("GS601"))
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
    fun resolvesSnapdragonPartNumberSuffixesBeforeBaseModels() {
        assertEquals("Snapdragon 7s Gen 4", MobileSocNameMapper.resolve("SM7635-AC"))
        assertEquals("Snapdragon 7s Gen 3", MobileSocNameMapper.resolve("SM7635"))
        assertEquals("Snapdragon 6s Gen 4", MobileSocNameMapper.resolve("SM6435-AA"))
        assertEquals("Snapdragon 6s Gen 3", MobileSocNameMapper.resolve("SM6375-AC"))
        assertEquals("Snapdragon 695", MobileSocNameMapper.resolve("SM6375"))
        assertEquals("Snapdragon 4 Gen 4", MobileSocNameMapper.resolve("SM4450-AF"))
        assertEquals("Snapdragon 4 Gen 2", MobileSocNameMapper.resolve("SM4450"))
        assertEquals("Snapdragon 6s 4G Gen 2", MobileSocNameMapper.resolve("SM6225-AF"))
        assertEquals("Snapdragon 685", MobileSocNameMapper.resolve("SM6225-AD"))
        assertEquals("Snapdragon 680 series", MobileSocNameMapper.resolve("SM6225"))
        assertEquals("Snapdragon 870", MobileSocNameMapper.resolve("SM8250-AC"))
        assertEquals("Snapdragon 855+", MobileSocNameMapper.resolve("SM8150-AC"))
    }

    @Test
    fun keepsHandheldsOnTheirActualSnapdragonSilicon() {
        assertEquals("Snapdragon 8 Gen 2", MobileSocNameMapper.resolve("SM8550", model = "AYANEO Pocket S"))
        assertEquals("Snapdragon 7+ Gen 3", MobileSocNameMapper.resolve("SM7675", model = "Retroid Pocket G2"))
        assertEquals("Snapdragon 8 Gen 3", MobileSocNameMapper.resolve("SM8650", model = "AYANEO Pocket S2"))
        assertEquals("Snapdragon 8 Gen 2", MobileSocNameMapper.resolve("SM8550", model = "Phone"))
    }

    @Test
    fun keepsAlreadyReadableMarketingNames() {
        assertEquals("Snapdragon 8 Gen 3", MobileSocNameMapper.resolve("Snapdragon 8 Gen 3"))
        assertEquals("Snapdragon 7s Gen 4", MobileSocNameMapper.resolve("Snapdragon 7s Gen 4"))
        assertEquals("Exynos 2400", MobileSocNameMapper.resolve("Exynos 2400"))
    }

    @Test
    fun prefersTheReportedMarketingNameOverAGenericPlatformCode() {
        assertEquals(
            "Snapdragon 7s Gen 4",
            MobileSocNameMapper.resolve("Snapdragon 7s Gen 4", board = "sm7635")
        )
    }

    @Test
    fun neverShowsAnUnmappedRawPlatformCode() {
        assertEquals("Snapdragon", MobileSocNameMapper.resolve("SM9999", manufacturer = "Qualcomm"))
        assertEquals("MediaTek", MobileSocNameMapper.resolve("MT9999"))
        assertEquals("Unknown SoC", MobileSocNameMapper.resolve("future_platform"))
    }

    @Test
    fun doesNotMapAllwinnerDevicesToProcessorNames() {
        assertEquals("Unknown SoC", MobileSocNameMapper.resolve("Allwinner"))
        assertEquals("Unknown SoC", MobileSocNameMapper.resolve("Allwinner H618"))
        assertEquals("Unknown SoC", MobileSocNameMapper.resolve("sun50iw9"))
        assertEquals(
            "Unknown SoC",
            MobileSocNameMapper.resolve(
                socModel = null,
                hardware = "unknown",
                manufacturer = "Allwinner",
                model = "Allwinner Game Console"
            )
        )
    }
}
