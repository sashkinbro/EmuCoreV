package com.sbro.emucorev.core

import java.util.Locale

object SocGpuCatalog {
    private data class Entry(val socName: String, val model: GpuModelInfo)

    private fun entry(socName: String, model: GpuModelInfo) = Entry(socName, model)

    private fun adreno(model: Int, suffix: String = ""): GpuModelInfo {
        val family = when {
            model >= 800 -> AdrenoFamily.A8XX
            model >= 700 -> AdrenoFamily.A7XX
            model >= 600 -> AdrenoFamily.A6XX
            else -> null
        }
        return GpuModelInfo(GpuVendor.ADRENO, "Adreno $model$suffix", family)
    }

    private fun adrenoNamed(name: String) = GpuModelInfo(GpuVendor.ADRENO, "Adreno $name")

    private fun mali(name: String) = GpuModelInfo(GpuVendor.MALI, name)

    private fun powerVr(name: String) = GpuModelInfo(GpuVendor.POWERVR, name)

    private fun xclipse(model: Int) = GpuModelInfo(GpuVendor.XCLIPSE, "Xclipse $model")

    private val socGpuEntries: List<Entry> = listOf(
        entry("Snapdragon 8 Elite Extreme Gen 6", adreno(850)),
        entry("Snapdragon 8 Elite Gen 6 Pro", adreno(850)),
        entry("Snapdragon 8 Elite Gen 6", adreno(845)),
        entry("Snapdragon 8 Elite Gen 5", adreno(840)),
        entry("Snapdragon 8 Gen 6", adreno(829)),
        entry("Snapdragon 8 Gen 5", adreno(829)),
        entry("Snapdragon 8 Elite", adreno(830)),
        entry("Snapdragon 8s Gen 4", adreno(825)),
        entry("Snapdragon 8 Gen 3", adreno(750)),
        entry("Snapdragon 8s Gen 3", adreno(735)),
        entry("Snapdragon 8 Gen 2", adreno(740)),
        entry("Snapdragon 8+ Gen 1", adreno(730)),
        entry("Snapdragon 8 Gen 1", adreno(730)),
        entry("Snapdragon 7 Gen 4", adreno(722)),
        entry("Snapdragon 7+ Gen 3", adreno(732)),
        entry("Snapdragon 7s Gen 4", adreno(810)),
        entry("Snapdragon 7s Gen 3", adreno(810)),
        entry("Snapdragon 7 Gen 3", adreno(720)),
        entry("Snapdragon 7+ Gen 2", adreno(725)),
        entry("Snapdragon 7s Gen 2", adreno(710)),
        entry("Snapdragon 7 Gen 1", adreno(644)),
        entry("Snapdragon 6 Gen 4", adreno(810)),
        entry("Snapdragon 6s Gen 4", adreno(710)),
        entry("Snapdragon 6 Gen 3", adreno(710)),
        entry("Snapdragon 6s Gen 3", adreno(619)),
        entry("Snapdragon 6 Gen 1", adreno(710)),
        entry("Snapdragon 6s 4G Gen 2", adreno(610)),
        entry("Snapdragon 6s Gen 2", adreno(610)),
        entry("Snapdragon 4s Gen 2", adreno(611)),
        entry("Snapdragon 4 Gen 4", adreno(613)),
        entry("Snapdragon 4 Gen 2", adreno(613)),
        entry("Snapdragon 4 Gen 1", adreno(619)),
        entry("Snapdragon 888", adreno(660)),
        entry("Snapdragon 870", adreno(650)),
        entry("Snapdragon 865", adreno(650)),
        entry("Snapdragon 855+", adreno(640)),
        entry("Snapdragon 860", adreno(640)),
        entry("Snapdragon 855", adreno(640)),
        entry("Snapdragon 845", adreno(630)),
        entry("Snapdragon 835", adreno(540)),
        entry("Snapdragon 820", adreno(530)),
        entry("Snapdragon 810", adreno(430)),
        entry("Snapdragon 808", adreno(418)),
        entry("Snapdragon 805", adreno(420)),
        entry("Snapdragon 800", adreno(330)),
        entry("Snapdragon 780G", adreno(642)),
        entry("Snapdragon 778G", adreno(642, "L")),
        entry("Snapdragon 765", adreno(620)),
        entry("Snapdragon 750G", adreno(619)),
        entry("Snapdragon 730", adreno(618)),
        entry("Snapdragon 720G", adreno(618)),
        entry("Snapdragon 712", adreno(616)),
        entry("Snapdragon 710", adreno(616)),
        entry("Snapdragon 695", adreno(619)),
        entry("Snapdragon 690", adreno(619, "L")),
        entry("Snapdragon 685", adreno(610)),
        entry("Snapdragon 680", adreno(610)),
        entry("Snapdragon 678", adreno(612)),
        entry("Snapdragon 675", adreno(612)),
        entry("Snapdragon 670", adreno(615)),
        entry("Snapdragon 665", adreno(610)),
        entry("Snapdragon 662", adreno(610)),
        entry("Snapdragon 660", adreno(512)),
        entry("Snapdragon 653", adreno(510)),
        entry("Snapdragon 652", adreno(510)),
        entry("Snapdragon 650", adreno(510)),
        entry("Snapdragon 636", adreno(509)),
        entry("Snapdragon 632", adreno(506)),
        entry("Snapdragon 630", adreno(508)),
        entry("Snapdragon 626", adreno(506)),
        entry("Snapdragon 625", adreno(506)),
        entry("Snapdragon 617", adreno(405)),
        entry("Snapdragon 616", adreno(405)),
        entry("Snapdragon 615", adreno(405)),
        entry("Snapdragon 480", adreno(619)),
        entry("Snapdragon 460", adreno(610)),
        entry("Snapdragon 450", adreno(506)),
        entry("Snapdragon 439", adreno(505)),
        entry("Snapdragon 435", adreno(505)),
        entry("Snapdragon 430", adreno(505)),
        entry("Snapdragon 429", adreno(504)),
        entry("Snapdragon 425", adreno(308)),
        entry("Snapdragon 415", adreno(306)),
        entry("Snapdragon 412", adreno(306)),
        entry("Snapdragon 410", adreno(306)),
        entry("Snapdragon 400", adreno(306)),
        entry("Snapdragon 215", adreno(308)),
        entry("Snapdragon 212", adreno(304)),
        entry("Snapdragon 210", adreno(304)),
        entry("Snapdragon 600", adreno(320)),
        entry("Snapdragon S4 Pro", adreno(320)),
        entry("Snapdragon 7c", adreno(618)),
        entry("Snapdragon 7c+ Gen 3", adreno(643)),
        entry("Snapdragon 8cx", adreno(680)),
        entry("Snapdragon 8cx Gen 3", adreno(690)),
        entry("Snapdragon XR2", adreno(650)),
        entry("Snapdragon X Elite", adrenoNamed("X1-85")),
        entry("Snapdragon G1 Gen 1", adrenoNamed("A11")),
        entry("Snapdragon G1 Gen 2", adrenoNamed("A11")),
        entry("Snapdragon G2 Gen 1", adrenoNamed("A21")),
        entry("Snapdragon G2 Gen 2", adrenoNamed("A21")),
        entry("Snapdragon G3 Gen 3", adrenoNamed("A33")),
        entry("Snapdragon G3x Gen 1", adrenoNamed("A32")),
        entry("Snapdragon G3x Gen 2", adrenoNamed("A32")),

        entry("Dimensity 9600 Pro", mali("Mali-G2 Ultra NX MP12")),
        entry("Dimensity 9600M", mali("Mali-G1 Ultra MC12")),
        entry("Dimensity 9500", mali("Mali-G1 Ultra MC12")),
        entry("Dimensity 9500s", mali("Immortalis-G925 MC11")),
        entry("Dimensity 9400+", mali("Immortalis-G925 MC12")),
        entry("Dimensity 9400e", mali("Immortalis-G925 MC12")),
        entry("Dimensity 9400", mali("Immortalis-G925 MC12")),
        entry("Dimensity 9300", mali("Immortalis-G720 MC12")),
        entry("Dimensity 9200", mali("Immortalis-G715 MC11")),
        entry("Dimensity 9000", mali("Mali-G710 MC10")),
        entry("Dimensity 8550", mali("Mali-G720 MC8")),
        entry("Dimensity 8500", mali("Mali-G720 MC8")),
        entry("Dimensity 8400 Ultra", mali("Mali-G720 MC7")),
        entry("Dimensity 8400", mali("Mali-G720 MC7")),
        entry("Dimensity 8350", mali("Mali-G615 MC6")),
        entry("Dimensity 8300", mali("Mali-G615 MC6")),
        entry("Dimensity 8200", mali("Mali-G610 MC6")),
        entry("Dimensity 8100", mali("Mali-G610 MC6")),
        entry("Dimensity 8000", mali("Mali-G610 MC6")),
        entry("Dimensity 1300", mali("Mali-G77 MC9")),
        entry("Dimensity 1200", mali("Mali-G77 MC9")),
        entry("Dimensity 1100", mali("Mali-G77 MC9")),
        entry("Dimensity 1080", mali("Mali-G68 MC4")),
        entry("Dimensity 1000", mali("Mali-G77 MC9")),
        entry("Dimensity 1000+", mali("Mali-G77 MC9")),
        entry("Dimensity 930", mali("Mali-G57 MC2")),
        entry("Dimensity 920", mali("Mali-G68 MC4")),
        entry("Dimensity 900", mali("Mali-G68 MC4")),
        entry("Dimensity 820", mali("Mali-G57 MC5")),
        entry("Dimensity 810", mali("Mali-G57 MC2")),
        entry("Dimensity 800", mali("Mali-G57 MC4")),
        entry("Dimensity 7400", mali("Mali-G615 MC2")),
        entry("Dimensity 7300", mali("Mali-G615 MC2")),
        entry("Dimensity 7200", mali("Mali-G610 MC4")),
        entry("Dimensity 720", mali("Mali-G57 MC3")),
        entry("Dimensity 7025", mali("Mali-G57 MC2")),
        entry("Dimensity 700", mali("Mali-G57 MC2")),
        entry("Dimensity 6400", mali("Mali-G57 MC2")),
        entry("Dimensity 6300", mali("Mali-G57 MC2")),
        entry("Dimensity 6100+", mali("Mali-G57 MC2")),
        entry("Dimensity 6080", mali("Mali-G57 MC2")),
        entry("Helio G200", mali("Mali-G57 MC2")),
        entry("Helio G100", mali("Mali-G57 MC2")),
        entry("Helio G99", mali("Mali-G57 MC2")),
        entry("Helio G96", mali("Mali-G57 MC2")),
        entry("Helio G91", mali("Mali-G52 MC2")),
        entry("Helio G81", mali("Mali-G52 MC2")),
        entry("Helio G95", mali("Mali-G76 MC4")),
        entry("Helio G90", mali("Mali-G76 MC4")),
        entry("Helio G88", mali("Mali-G52 MC2")),
        entry("Helio G85", mali("Mali-G52 MC2")),
        entry("Helio G80", mali("Mali-G52 MC2")),
        entry("Helio G70", mali("Mali-G52 MC2")),
        entry("Helio G37", powerVr("PowerVR GE8320")),
        entry("Helio G35", powerVr("PowerVR GE8320")),
        entry("Helio G25", powerVr("PowerVR GE8320")),
        entry("Helio P90", powerVr("PowerVR GM9446")),
        entry("Helio P70", mali("Mali-G72 MP3")),
        entry("Helio P60", mali("Mali-G72 MP3")),
        entry("Helio P35", powerVr("PowerVR GE8320")),
        entry("Helio P30", mali("Mali-G71 MP2")),
        entry("Helio P25", mali("Mali-T880 MP2")),
        entry("Helio P23", mali("Mali-G71 MP2")),
        entry("Helio P22", powerVr("PowerVR GE8320")),
        entry("Helio P20", mali("Mali-T880 MP2")),
        entry("Helio P10", mali("Mali-T860 MP2")),
        entry("Helio X30", powerVr("PowerVR GT7400")),
        entry("Helio X20", mali("Mali-T880 MP4")),
        entry("MediaTek MT8183", mali("Mali-G72 MP3")),
        entry("MediaTek MT8176", powerVr("PowerVR GX6250")),
        entry("MediaTek MT8173", powerVr("PowerVR GX6250")),
        entry("MediaTek MT6739", powerVr("PowerVR GE8100")),
        entry("MediaTek MT6738", mali("Mali-T720")),
        entry("MediaTek MT6737", mali("Mali-T720")),
        entry("MediaTek MT6735", mali("Mali-T720")),
        entry("MediaTek MT6732", mali("Mali-T760")),
        entry("MediaTek MT6753", mali("Mali-T720")),
        entry("MediaTek MT6752", mali("Mali-T760")),
        entry("MediaTek MT6750", mali("Mali-T860")),
        entry("MediaTek MT6595", powerVr("PowerVR G6200")),
        entry("MediaTek MT6592", mali("Mali-450")),
        entry("MediaTek MT6589", powerVr("PowerVR SGX544")),
        entry("MediaTek MT6582", mali("Mali-400")),
        entry("MediaTek MT6577", powerVr("PowerVR SGX531")),
        entry("MediaTek MT6575", powerVr("PowerVR SGX531")),
        entry("MediaTek MT6572", mali("Mali-400")),

        entry("Exynos 2600", xclipse(960)),
        entry("Exynos 2500", xclipse(950)),
        entry("Exynos 2400e", xclipse(940)),
        entry("Exynos 2400", xclipse(940)),
        entry("Exynos 2200", xclipse(920)),
        entry("Exynos 1680", xclipse(550)),
        entry("Exynos 1580", xclipse(540)),
        entry("Exynos 1480", xclipse(530)),
        entry("Exynos 2100", mali("Mali-G78 MP14")),
        entry("Exynos 1080", mali("Mali-G78 MP10")),
        entry("Exynos 990", mali("Mali-G77 MP11")),
        entry("Exynos 9825", mali("Mali-G76 MP12")),
        entry("Exynos 9820", mali("Mali-G76 MP12")),
        entry("Exynos 9810", mali("Mali-G72 MP18")),
        entry("Exynos 980", mali("Mali-G76 MP5")),
        entry("Exynos 9611", mali("Mali-G72 MP3")),
        entry("Exynos 9610", mali("Mali-G72 MP3")),
        entry("Exynos 9609", mali("Mali-G72 MP3")),
        entry("Exynos 8895", mali("Mali-G71 MP20")),
        entry("Exynos 8890", mali("Mali-T880 MP12")),
        entry("Exynos 850", mali("Mali-G52 MP1")),
        entry("Exynos 7904", mali("Mali-G71 MP2")),
        entry("Exynos 7885", mali("Mali-G71 MP2")),
        entry("Exynos 7880", mali("Mali-T830 MP2")),
        entry("Exynos 7872", mali("Mali-G71 MP1")),
        entry("Exynos 7870", mali("Mali-T830 MP1")),
        entry("Exynos 7580", mali("Mali-T720 MP2")),
        entry("Exynos 7570", mali("Mali-T720 MP1")),
        entry("Exynos 7420", mali("Mali-T760 MP8")),
        entry("Exynos 5433", mali("Mali-T760 MP6")),
        entry("Exynos 5422", mali("Mali-T628 MP6")),
        entry("Exynos 5420", mali("Mali-T628 MP6")),
        entry("Exynos 5410", powerVr("PowerVR SGX544MP3")),
        entry("Exynos 5250", mali("Mali-T604")),
        entry("Exynos 4412", mali("Mali-400 MP4")),
        entry("Exynos 4212", mali("Mali-400 MP4")),
        entry("Exynos 4210", mali("Mali-400 MP4")),
        entry("Exynos 1380", mali("Mali-G68 MP5")),
        entry("Exynos 1330", mali("Mali-G68 MP2")),
        entry("Exynos 1280", mali("Mali-G68 MP4")),

        entry("Google Tensor G6", powerVr("PowerVR CXTP-48-1536")),
        entry("Tensor G6", powerVr("PowerVR CXTP-48-1536")),
        entry("Google Tensor G5", powerVr("PowerVR DXT-48-1536")),
        entry("Tensor G5", powerVr("PowerVR DXT-48-1536")),
        entry("Google Tensor G4", mali("Mali-G715 MP7")),
        entry("Tensor G4", mali("Mali-G715 MP7")),
        entry("Google Tensor G3", mali("Mali-G715 MP7")),
        entry("Tensor G3", mali("Mali-G715 MP7")),
        entry("Google Tensor G2", mali("Mali-G710 MP7")),
        entry("Tensor G2", mali("Mali-G710 MP7")),
        entry("Google Tensor", mali("Mali-G78 MP20")),
        entry("Tensor", mali("Mali-G78 MP20")),

        entry("Kirin 9020", GpuModelInfo(GpuVendor.UNKNOWN, "Maleoon 920")),
        entry("Kirin 9000S", GpuModelInfo(GpuVendor.UNKNOWN, "Maleoon 910")),
        entry("Kirin 990", mali("Mali-G76 MP16")),
        entry("Kirin 980", mali("Mali-G76 MP10")),
        entry("Kirin 970", mali("Mali-G72 MP12")),
        entry("Kirin 960", mali("Mali-G71 MP8")),
        entry("Kirin 955", mali("Mali-T880 MP4")),
        entry("Kirin 950", mali("Mali-T880 MP4")),
        entry("Kirin 935", mali("Mali-T628 MP4")),
        entry("Kirin 820", mali("Mali-G57 MP6")),
        entry("Kirin 810", mali("Mali-G52 MP6")),
        entry("Kirin 710", mali("Mali-G51 MP4")),
        entry("Kirin 650", mali("Mali-G51 MP4")),

        entry("Unisoc T9100", mali("Mali-G57 MC4")),
        entry("Unisoc T8300", mali("Mali-G57 MC2")),
        entry("Unisoc T820", mali("Mali-G57 MC4")),
        entry("Unisoc T810", mali("Mali-G57 MC2")),
        entry("Unisoc T770", mali("Mali-G57 MC4")),
        entry("Unisoc T765", mali("Mali-G57 MC4")),
        entry("Unisoc T760", mali("Mali-G57 MC4")),
        entry("Unisoc T750", mali("Mali-G57 MC2")),
        entry("Unisoc T7300", mali("Mali-G57 MC2")),
        entry("Unisoc T7280", mali("Mali-G57 MC2")),
        entry("Unisoc T7250", mali("Mali-G57 MC2")),
        entry("Unisoc T710", powerVr("PowerVR GM9446")),
        entry("Unisoc T700", powerVr("PowerVR GM9446")),
        entry("Unisoc T620", mali("Mali-G57 MP2")),
        entry("Unisoc T618", mali("Mali-G52 MP2")),
        entry("Unisoc T616", mali("Mali-G57 MP1")),
        entry("Unisoc T612", mali("Mali-G57 MP1")),
        entry("Unisoc T610", mali("Mali-G52 MP2")),
        entry("Unisoc T606", mali("Mali-G57 MP1")),
        entry("Unisoc T603", mali("Mali-G57 MP1")),
        entry("Unisoc T310", powerVr("PowerVR GE8300")),
        entry("Unisoc SC9863A", powerVr("PowerVR GE8322")),
        entry("Unisoc SC9863", powerVr("PowerVR GE8322")),

        entry("Unisoc SC9832E", mali("Mali-T820 MP1")),
        entry("Spreadtrum SC9860", mali("Mali-T760 MP4")),
        entry("Spreadtrum SC9850", mali("Mali-T720 MP2")),
        entry("Spreadtrum SC9830", mali("Mali-400 MP2")),
        entry("Spreadtrum SC8830", mali("Mali-400 MP2")),
        entry("Spreadtrum SC7731", mali("Mali-400 MP1")),

        entry("Kirin K3V2", GpuModelInfo(GpuVendor.UNKNOWN, "Vivante GC4000")),

        entry("Rockchip RK3588", mali("Mali-G610 MC4")),
        entry("Rockchip RK3576", mali("Mali-G52 MC3")),
        entry("Rockchip RK3568", mali("Mali-G52 MP2")),
        entry("Rockchip RK3566", mali("Mali-G52 MP2")),
        entry("Rockchip RK3562", mali("Mali-G52 2EE")),
        entry("Rockchip RK3528", mali("Mali-450 MP2")),
        entry("Rockchip RK3399", mali("Mali-T860 MP4")),
        entry("Rockchip RK3368", powerVr("PowerVR G6110")),
        entry("Rockchip RK3328", mali("Mali-450 MP2")),
        entry("Rockchip RK3326", mali("Mali-450 MP2")),
        entry("Rockchip RK3288", mali("Mali-T760 MP4")),
        entry("Rockchip RK3229", mali("Mali-400 MP2")),
        entry("Rockchip RK3188", mali("Mali-400 MP4")),
        entry("Rockchip RK3128", mali("Mali-400 MP2")),
        entry("Rockchip RK3126", mali("Mali-400 MP2")),

        entry("Amlogic S928X", mali("Mali-G57 MC2")),
        entry("Amlogic A311D", mali("Mali-G52 MP4")),
        entry("Amlogic S922X", mali("Mali-G52 MP4")),
        entry("Amlogic S912", mali("Mali-T820 MP3")),
        entry("Amlogic S905X4", mali("Mali-G31 MP2")),
        entry("Amlogic S905X3", mali("Mali-G31 MP2")),
        entry("Amlogic S905X2", mali("Mali-G31 MP2")),
        entry("Amlogic S905W", mali("Mali-450 MP3")),
        entry("Amlogic S905", mali("Mali-450 MP3")),

        entry("NVIDIA Tegra X1", GpuModelInfo(GpuVendor.UNKNOWN, "NVIDIA Maxwell")),
        entry("NVIDIA Tegra K1", GpuModelInfo(GpuVendor.UNKNOWN, "NVIDIA Kepler")),
        entry("NVIDIA Tegra 4", GpuModelInfo(GpuVendor.UNKNOWN, "NVIDIA ULP GeForce")),
        entry("NVIDIA Tegra 3", GpuModelInfo(GpuVendor.UNKNOWN, "NVIDIA ULP GeForce"))
    )

    private val bySocName: Map<String, GpuModelInfo> = buildMap {
        socGpuEntries.forEach { entry -> put(normalize(entry.socName), entry.model) }
    }

    fun forSoc(socName: String?): GpuModelInfo? {
        if (socName.isNullOrBlank()) return null
        nameVariants(socName).forEach { variant ->
            bySocName[variant]?.let { return it }
        }
        return null
    }

    private fun nameVariants(socName: String): List<String> {
        val normalized = normalize(socName)
        if ('/' !in normalized) return listOf(normalized)
        val prefix = normalized.substringBefore('/').trim().substringBeforeLast(' ').trim()
        val variants = mutableListOf(normalized)
        normalized.split('/').forEachIndexed { index, part ->
            val value = part.trim()
            if (value.isEmpty()) return@forEachIndexed
            variants += when {
                index == 0 || value.contains(' ') -> value
                else -> "$prefix $value"
            }
        }
        return variants
    }

    private fun normalize(socName: String): String = socName
        .lowercase(Locale.US)
        .replace("(tm)", " ")
        .replace("®", " ")
        .replace("™", " ")
        .replace("qualcomm", " ")
        .replace("mediatek", " ")
        .replace("samsung", " ")
        .replace("google", " ")
        .replace("hisilicon", " ")
        .replace(Regex("\\b(for galaxy|leading version)\\b"), " ")
        .replace(Regex("\\b5g\\b"), " ")
        .replace(Regex("\\b(series|family|mobile platform|processor)\\b"), " ")
        .replace(Regex("[^a-z0-9+/ ]"), " ")
        .replace(Regex("\\s+"), " ")
        .trim()
}
