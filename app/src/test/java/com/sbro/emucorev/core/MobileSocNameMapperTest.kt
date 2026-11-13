package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
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
    fun keepsUnspecifiedAllwinnerPartsAtTheHonestFamilyLevel() {
        assertEquals("Allwinner", MobileSocNameMapper.resolve("Allwinner"))
        assertEquals("Allwinner H618", MobileSocNameMapper.resolve("Allwinner H618"))
        assertEquals("Allwinner H616/H618/H700", MobileSocNameMapper.resolve("sun50iw9"))
        assertEquals(
            "Allwinner",
            MobileSocNameMapper.resolve(
                socModel = null,
                hardware = "unknown",
                manufacturer = "Allwinner",
                model = "Allwinner Game Console"
            )
        )
    }

    @Test
    fun codeAliasesRejectEmbeddedWordsAndLongerNumbers() {
        listOf("landscape", "konafoo", "notrk3566", "SM85500", "MT67680", "T6180", "RK35660", "éSM8550", "SM8550я")
            .forEach { assertNull(it, MobileSocNameMapper.resolveKnownCode(it)) }
        listOf("A5230", "notA523", "H7000", "Samsung SM-A527", "Samsung SM_A527")
            .forEach { assertNull(it, MobileSocNameMapper.resolveKnownCode(it)) }
    }

    @Test
    fun authoritativeSocCodeBeatsLaterDeviceMarketingText() {
        assertEquals("Snapdragon 8 Gen 3", MobileSocNameMapper.resolve("SM8650", model = "Snapdragon 8 Gen 2"))
        assertEquals("Snapdragon 8 Gen 3", MobileSocNameMapper.resolve("SM8650", model = "Allwinner A523"))
    }

    @Test
    fun resolvesExplicitAllwinnerPartsAndSharedBspFamilies() {
        listOf("A523", "A527", "T527", "H728", "A333", "A537", "MR527", "A133", "A133P", "H700", "H616", "H618")
            .forEach { assertEquals("Allwinner $it", MobileSocNameMapper.resolve(it)) }
        assertEquals("Allwinner A537", MobileSocNameMapper.resolve("Allwinner A537PX"))
        assertEquals("Allwinner T527", MobileSocNameMapper.resolve("T527M02"))
        assertEquals("Allwinner A523", MobileSocNameMapper.resolve("allwinner,sun55i-a523"))
        assertEquals("Allwinner A523/A527/H728/T527", MobileSocNameMapper.resolve("sun55iw3p1"))
        assertEquals("Allwinner H616/H618/H700", MobileSocNameMapper.resolve("sun50iw9"))
        assertEquals("Allwinner A100/A133", MobileSocNameMapper.resolve("sun50iw10p1"))
    }

    @Test
    fun narrowHandheldFallbackDoesNotOverrideExplicitSilicon() {
        listOf("TG5050", "TrimUI Smart Pro S").forEach {
            assertEquals("Allwinner A523", MobileSocNameMapper.resolve(null, hardware = "sun55iw3p1", model = it))
        }
        assertEquals("Allwinner T527", MobileSocNameMapper.resolve("T527", model = "TG5050"))
        assertEquals("Unknown SoC", MobileSocNameMapper.resolve(null, model = "TrimUI Smart Pro"))
    }

    @Test
    fun marketingBrandsDoNotMatchInsideWordsOrSwallowDeviceTreeFamilies() {
        assertEquals("Unknown SoC", MobileSocNameMapper.resolve("NotSnapdragon123"))
        assertEquals("Unknown SoC", MobileSocNameMapper.resolve("unrelatedexynos2400"))
        listOf("SMALL", "MTVdevice", "hiking", "NotAllwinner123", "notzuma").forEach {
            assertEquals(it, "Unknown SoC", MobileSocNameMapper.resolve(it))
        }
        assertEquals("Allwinner A523/A527/H728/T527", MobileSocNameMapper.resolve("allwinner,sun55iw3"))
    }

    @Test
    fun boundedSunBoardAliasDoesNotMatchOtherNames() {
        assertEquals("Snapdragon 8 Elite", MobileSocNameMapper.resolve(null, hardware = "qcom", board = "sun"))
        listOf("samsung", "sunmi", "sunstone").forEach {
            assertEquals(it, "Unknown SoC", MobileSocNameMapper.resolve(it))
        }
    }

    @Test
    fun supplementalIdentifiersRespectAuthorityAndExplicitPartsBeatSharedFamilies() {
        assertEquals("Snapdragon 8 Gen 3", MobileSocNameMapper.resolve("SM8650",
            hardware = "SM8550", additionalIdentifiers = listOf("A523")))
        assertEquals("Snapdragon 8 Elite", MobileSocNameMapper.resolve("unknown",
            hardware = "SM8650", additionalIdentifiers = listOf("", "qcom,sm8750")))
        assertEquals("Allwinner A537", MobileSocNameMapper.resolve(null,
            hardware = "sun55iw3", additionalIdentifiers = listOf("Allwinner", "A537")))
        assertEquals("Allwinner T527", MobileSocNameMapper.resolve("allwinner,sun55iw3",
            additionalIdentifiers = listOf("T527"), model = "TG5050"))
        assertEquals("Snapdragon 8 Gen 2", MobileSocNameMapper.resolve("SM-8550"))
        assertNull(MobileSocNameMapper.resolveKnownCode("SM-A527"))
    }
    @Test
    fun allwinnerDeviceMarketingNumbersDoNotIdentifySilicon() {
        listOf("Allwinner Game Console 2025", "Allwinner Handheld 2", "Allwinner Model 133").forEach {
            assertEquals(it, "Allwinner", MobileSocNameMapper.resolve(null, manufacturer = "Allwinner", model = it))
        }
        assertEquals("Allwinner A64", MobileSocNameMapper.resolve("Allwinner A64"))
        assertEquals("Allwinner H618", MobileSocNameMapper.resolve("Allwinner H618"))
    }

    @Test
    fun samsungDevicePrefixSeparatorsCannotBecomeAllwinnerAliases() {
        listOf("SM - A527", "SM  A527", "SM_ - A527", "SM / A527", "SM\t-\tA527").forEach {
            assertNull(it, MobileSocNameMapper.resolveKnownCode("Samsung $it"))
            assertEquals(it, "Samsung Exynos", MobileSocNameMapper.resolve(it, manufacturer = "Samsung"))
        }
        assertEquals("Snapdragon 8 Gen 2", MobileSocNameMapper.resolve("SM - 8550"))
        assertEquals("Allwinner A527", MobileSocNameMapper.resolve("SM unrelated A527"))
        assertEquals("Allwinner H700", MobileSocNameMapper.resolveKnownCode("Samsung SM - A527; Allwinner H700"))
    }

}
