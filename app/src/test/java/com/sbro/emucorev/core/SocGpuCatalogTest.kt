package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SocGpuCatalogTest {
    @Test
    fun `maps Snapdragon marketing names to exact Adreno models`() {
        assertEquals("Adreno 840", SocGpuCatalog.forSoc("Snapdragon 8 Elite Gen 5")?.displayName)
        assertEquals("Adreno 830", SocGpuCatalog.forSoc("Snapdragon 8 Elite")?.displayName)
        assertEquals("Adreno 750", SocGpuCatalog.forSoc("Snapdragon 8 Gen 3")?.displayName)
        assertEquals("Adreno 730", SocGpuCatalog.forSoc("Snapdragon 8+ Gen 1")?.displayName)
        assertEquals("Adreno 730", SocGpuCatalog.forSoc("Snapdragon 8 Gen 1")?.displayName)
        assertEquals("Adreno 710", SocGpuCatalog.forSoc("Snapdragon 6 Gen 1")?.displayName)
        assertEquals("Adreno 619", SocGpuCatalog.forSoc("Snapdragon 695")?.displayName)
        assertEquals("Adreno 619L", SocGpuCatalog.forSoc("Snapdragon 690")?.displayName)
        assertEquals("Adreno 610", SocGpuCatalog.forSoc("Snapdragon 680 series")?.displayName)
        assertEquals("Adreno 613", SocGpuCatalog.forSoc("Snapdragon 4 Gen 2")?.displayName)
        assertEquals("Adreno 619", SocGpuCatalog.forSoc("Snapdragon 4 Gen 1")?.displayName)
        assertEquals("Adreno 642L", SocGpuCatalog.forSoc("Snapdragon 778G series")?.displayName)
        assertEquals("Adreno 740", SocGpuCatalog.forSoc("SNAPDRAGON 8 GEN 2")?.displayName)
        assertEquals("Adreno 722", SocGpuCatalog.forSoc("Snapdragon 7 Gen 4")?.displayName)
        assertEquals("Adreno 810", SocGpuCatalog.forSoc("Snapdragon 7s Gen 4")?.displayName)
        assertEquals("Adreno 810", SocGpuCatalog.forSoc("Snapdragon 7s Gen 3")?.displayName)
        assertEquals("Adreno 720", SocGpuCatalog.forSoc("Snapdragon 7 Gen 3")?.displayName)
        assertEquals("Adreno 810", SocGpuCatalog.forSoc("Snapdragon 6 Gen 4")?.displayName)
        assertEquals("Adreno 710", SocGpuCatalog.forSoc("Snapdragon 6s Gen 4")?.displayName)
        assertEquals("Adreno 619", SocGpuCatalog.forSoc("Snapdragon 6s Gen 3")?.displayName)
        assertEquals("Adreno 611", SocGpuCatalog.forSoc("Snapdragon 4s Gen 2")?.displayName)
        assertEquals("Adreno 613", SocGpuCatalog.forSoc("Snapdragon 4 Gen 4")?.displayName)
        assertEquals("Adreno 610", SocGpuCatalog.forSoc("Snapdragon 6s 4G Gen 2")?.displayName)
        assertEquals("Adreno 650", SocGpuCatalog.forSoc("Snapdragon 870")?.displayName)
        assertEquals("Adreno 640", SocGpuCatalog.forSoc("Snapdragon 855+")?.displayName)
        assertEquals("Adreno 850", SocGpuCatalog.forSoc("Snapdragon 8 Elite Extreme Gen 6")?.displayName)
        assertEquals("Adreno 845", SocGpuCatalog.forSoc("Snapdragon 8 Elite Gen 6")?.displayName)
        assertEquals("Adreno 829", SocGpuCatalog.forSoc("Snapdragon 8 Gen 6")?.displayName)
    }

    @Test
    fun `matches only the exact SoC instead of neighbouring generations`() {
        assertEquals("Adreno 810", SocGpuCatalog.forSoc("Qualcomm Snapdragon 7s Gen 4")?.displayName)
        assertEquals("Adreno 722", SocGpuCatalog.forSoc("Snapdragon 7 Gen 4")?.displayName)
        assertEquals("Adreno 730", SocGpuCatalog.forSoc("Snapdragon 8 Gen 1")?.displayName)
        assertEquals("Adreno 740", SocGpuCatalog.forSoc("Snapdragon 8 Gen 2")?.displayName)
        assertEquals("Adreno 750", SocGpuCatalog.forSoc("Snapdragon 8 Gen 3")?.displayName)
        assertEquals("Adreno 740", SocGpuCatalog.forSoc("Snapdragon 8 Gen 2 for Galaxy")?.displayName)
        assertEquals("Adreno 740", SocGpuCatalog.forSoc("Snapdragon 8 Gen 2 5G")?.displayName)
        assertNull(SocGpuCatalog.forSoc("Snapdragon 7s Gen 9"))
        assertNull(SocGpuCatalog.forSoc("Snapdragon 8 Gen 99"))
    }

    @Test
    fun `maps MediaTek marketing names without substring collisions`() {
        assertEquals("Mali-G610 MC6", SocGpuCatalog.forSoc("Dimensity 8000/8100")?.displayName)
        assertEquals("Mali-G57 MC4", SocGpuCatalog.forSoc("Dimensity 800 series")?.displayName)
        assertEquals("Mali-G610 MC4", SocGpuCatalog.forSoc("Dimensity 7200 series")?.displayName)
        assertEquals("Mali-G57 MC3", SocGpuCatalog.forSoc("Dimensity 720 series")?.displayName)
        assertEquals("Immortalis-G720 MC12", SocGpuCatalog.forSoc("Dimensity 9300 series")?.displayName)
        assertEquals("Mali-G57 MC2", SocGpuCatalog.forSoc("Dimensity 930")?.displayName)
        assertEquals("Mali-G68 MC4", SocGpuCatalog.forSoc("Dimensity 900/920/1080")?.displayName)
        assertEquals("Mali-G57 MC2", SocGpuCatalog.forSoc("Helio G99/G100")?.displayName)
        assertEquals("Mali-G52 MC2", SocGpuCatalog.forSoc("Helio G70/G80/G85/G88")?.displayName)
        assertEquals("PowerVR GM9446", SocGpuCatalog.forSoc("Helio P90/G90")?.displayName)
        assertEquals("Mali-G76 MC4", SocGpuCatalog.forSoc("Helio G90/G95")?.displayName)
        assertEquals("PowerVR GE8320", SocGpuCatalog.forSoc("Helio P35/G35/G37")?.displayName)
    }

    @Test
    fun `maps Exynos Tensor Kirin and Unisoc names`() {
        assertEquals("Xclipse 920", SocGpuCatalog.forSoc("Exynos 2200")?.displayName)
        assertEquals("Xclipse 530", SocGpuCatalog.forSoc("Exynos 1480")?.displayName)
        assertEquals("Mali-G52 MP1", SocGpuCatalog.forSoc("Exynos 850")?.displayName)
        assertEquals("Mali-G715 MP7", SocGpuCatalog.forSoc("Google Tensor G3")?.displayName)
        assertEquals("PowerVR DXT-48-1536", SocGpuCatalog.forSoc("Google Tensor G5")?.displayName)
        assertEquals("PowerVR CXTP-48-1536", SocGpuCatalog.forSoc("Google Tensor G6")?.displayName)
        assertEquals("Xclipse 550", SocGpuCatalog.forSoc("Exynos 1680")?.displayName)
        assertEquals("Xclipse 940", SocGpuCatalog.forSoc("Exynos 2400e")?.displayName)
        assertEquals("Mali-G1 Ultra MC12", SocGpuCatalog.forSoc("Dimensity 9500")?.displayName)
        assertEquals("Immortalis-G925 MC11", SocGpuCatalog.forSoc("Dimensity 9500s")?.displayName)
        assertEquals("Mali-G720 MC8", SocGpuCatalog.forSoc("Dimensity 8500")?.displayName)
        assertEquals("Mali-G720 MC7", SocGpuCatalog.forSoc("Dimensity 8400 Ultra")?.displayName)
        assertEquals("Mali-G2 Ultra NX MP12", SocGpuCatalog.forSoc("Dimensity 9600 Pro")?.displayName)
        assertEquals("Mali-G78 MP20", SocGpuCatalog.forSoc("Google Tensor")?.displayName)
        assertEquals("Mali-G51 MP4", SocGpuCatalog.forSoc("Kirin 650/710 series")?.displayName)
        assertEquals("Mali-G57 MP1", SocGpuCatalog.forSoc("Unisoc T606/T612")?.displayName)
        assertEquals("Mali-G57 MC4", SocGpuCatalog.forSoc("Unisoc T760")?.displayName)
    }

    @Test
    fun `every SoC known to the name mapper has a GPU mapping`() {
        val missing = MobileSocNameMapper.knownSocNames()
            .filter { SocGpuCatalog.forSoc(it) == null }
        assertTrue("Missing GPU mappings: $missing", missing.isEmpty())
    }

    @Test
    fun `returns null for unknown SoCs and blank input`() {
        assertNull(SocGpuCatalog.forSoc("Unknown SoC"))
        assertNull(SocGpuCatalog.forSoc("Allwinner H618"))
        assertEquals("Mali-450 MP2", SocGpuCatalog.forSoc("Rockchip RK3528")?.displayName)
        assertNull(SocGpuCatalog.forSoc(null))
        assertNull(SocGpuCatalog.forSoc(""))
        assertNull(SocGpuCatalog.forSoc("   "))
    }
}
