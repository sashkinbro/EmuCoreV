package com.sbro.emucorev.core

import android.os.Build
import java.util.Locale

/** Converts Android SoC/platform identifiers into names suitable for the performance overlay. */
object MobileSocNameMapper {
    /**
     * Gaming platforms often expose the phone SoC they are derived from (for example SM8550)
     * instead of their Snapdragon G-series retail name. Keep these model overrides separate so
     * phones using the same silicon are not mislabeled as handheld gaming platforms.
     */
    private val handheldModelNames: Map<String, String> = mapOf(
        "RETROIDPOCKETG2" to "Snapdragon G2 Gen 2",
        "RETROIDPOCKETCLASSIC" to "Snapdragon G1 Gen 2",
        "AYANEOPOCKETS2" to "Snapdragon G3 Gen 3",
        "KONKRPOCKETFITG3" to "Snapdragon G3 Gen 3",
        "ONEXSUGARSUGAR1" to "Snapdragon G3 Gen 3",
        "AYANEOPOCKETS" to "Snapdragon G3x Gen 2",
        "AYANEOPOCKETEVO" to "Snapdragon G3x Gen 2",
        "AYANEOPOCKETDMG" to "Snapdragon G3x Gen 2",
        "AYANEOPOCKETACE" to "Snapdragon G3x Gen 2",
        "AYANEOPOCKETDS" to "Snapdragon G3x Gen 2",
        "RAZEREDGE" to "Snapdragon G3x Gen 1",
        "RAZEREDGE5G" to "Snapdragon G3x Gen 1",
        "RAZEREDGEWIFI" to "Snapdragon G3x Gen 1",
        "PIMAXPORTAL" to "Snapdragon XR2",
        "PIMAXPORTALQLED" to "Snapdragon XR2",
        "PIMAXPORTALRETRO" to "Snapdragon XR2",
        "AYANEOPOCKETMICRO" to "Helio G99",
        "AYANEOPOCKETMICRO2" to "Snapdragon 865 series"
    )

    private val socNames: Map<String, String> = buildMap {
        // Qualcomm gaming/XR platforms. The full marketing-name aliases cover devices which
        // expose ro.soc.model as text; SG8175P is the dedicated G3x Gen 1 part identifier.
        put("SNAPDRAGONG3GEN3", "Snapdragon G3 Gen 3")
        put("SNAPDRAGONG2GEN2", "Snapdragon G2 Gen 2")
        put("SNAPDRAGONG1GEN2", "Snapdragon G1 Gen 2")
        put("SNAPDRAGONG3XGEN2", "Snapdragon G3x Gen 2")
        put("SNAPDRAGONG3XGEN1", "Snapdragon G3x Gen 1")
        put("SNAPDRAGONG2GEN1", "Snapdragon G2 Gen 1")
        put("SNAPDRAGONG1GEN1", "Snapdragon G1 Gen 1")
        put("SG8175P", "Snapdragon G3x Gen 1")
        put("SNAPDRAGONXR2", "Snapdragon XR2")

        // Qualcomm Snapdragon 8 series.
        put("SM8850", "Snapdragon 8 Elite Gen 5")
        put("SM8845", "Snapdragon 8 Gen 5")
        put("SM8750", "Snapdragon 8 Elite")
        put("SM8735", "Snapdragon 8s Gen 4")
        put("SM8650", "Snapdragon 8 Gen 3")
        put("SM8635", "Snapdragon 8s Gen 3")
        put("SM8550", "Snapdragon 8 Gen 2")
        put("SM8475", "Snapdragon 8+ Gen 1")
        put("SM8450", "Snapdragon 8 Gen 1")
        put("SM8350", "Snapdragon 888 series")
        put("SM8250", "Snapdragon 865 series")
        put("SM8150", "Snapdragon 855 series")
        put("SDM845", "Snapdragon 845")
        put("MSM8998", "Snapdragon 835")
        put("MSM8996", "Snapdragon 820 series")
        put("MSM8994", "Snapdragon 810")
        put("MSM8992", "Snapdragon 808")
        put("MSM8974", "Snapdragon 800 series")

        // Qualcomm Snapdragon 7 series.
        put("SM7750", "Snapdragon 7 Gen 4")
        put("SM7675", "Snapdragon 7+ Gen 3")
        put("SM7635", "Snapdragon 7s Gen 3")
        put("SM7550", "Snapdragon 7 Gen 3")
        put("SM7475", "Snapdragon 7+ Gen 2")
        put("SM7435", "Snapdragon 7s Gen 2")
        put("SM7450", "Snapdragon 7 Gen 1")
        put("SM7350", "Snapdragon 780G")
        put("SM7325", "Snapdragon 778G series")
        put("SM7225", "Snapdragon 750G")
        put("SM7125", "Snapdragon 720G")
        put("SM7250", "Snapdragon 765 series")
        put("SM7150", "Snapdragon 730 series")
        put("SDM765", "Snapdragon 765 series")
        put("SDM730", "Snapdragon 730 series")
        put("SDM712", "Snapdragon 712")
        put("SDM710", "Snapdragon 710")

        // Qualcomm Snapdragon 6, 4 and older mid-range series.
        put("SM6650", "Snapdragon 6 Gen 4")
        put("SM6475", "Snapdragon 6 Gen 3")
        put("SM6450", "Snapdragon 6 Gen 1")
        put("SM6375", "Snapdragon 695")
        put("SM6350", "Snapdragon 690")
        put("SM6225", "Snapdragon 680 series")
        put("SM6125", "Snapdragon 665")
        put("SM6115", "Snapdragon 662")
        put("SM4635", "Snapdragon 4s Gen 2")
        put("SM4450", "Snapdragon 4 Gen 2")
        put("SM4375", "Snapdragon 4 Gen 1")
        put("SM4350", "Snapdragon 480 series")
        put("SM4250", "Snapdragon 460")
        put("SM4125", "Snapdragon 450")
        put("SDM675", "Snapdragon 675")
        put("SDM670", "Snapdragon 670")
        put("SDM660", "Snapdragon 660")
        put("SDM653", "Snapdragon 653")
        put("SDM652", "Snapdragon 652")
        put("SDM650", "Snapdragon 650")
        put("SDM636", "Snapdragon 636")
        put("SDM632", "Snapdragon 632")
        put("SDM630", "Snapdragon 630")
        put("SDM626", "Snapdragon 626")
        put("SDM625", "Snapdragon 625")
        put("SDM450", "Snapdragon 450")
        put("SDM439", "Snapdragon 439")
        put("SDM429", "Snapdragon 429")
        put("MSM8976", "Snapdragon 652/653")
        put("MSM8956", "Snapdragon 650")
        put("MSM8952", "Snapdragon 617")
        put("MSM8953", "Snapdragon 625 series")
        put("MSM8940", "Snapdragon 435")
        put("MSM8937", "Snapdragon 430 series")
        put("MSM8939", "Snapdragon 615/616")
        put("MSM8929", "Snapdragon 415")
        put("MSM8928", "Snapdragon 400")
        put("MSM8926", "Snapdragon 400")
        put("MSM8916", "Snapdragon 410/412")
        put("MSM8917", "Snapdragon 425")
        put("MSM8909", "Snapdragon 210/212")
        put("APQ8084", "Snapdragon 805")
        put("APQ8064T", "Snapdragon 600")
        put("APQ8064", "Snapdragon S4 Pro")
        put("QCS8550", "Snapdragon 8 Gen 2")
        put("QCM8550", "Snapdragon 8 Gen 2")
        put("QCS8250", "Snapdragon 865 series")
        put("QCM8250", "Snapdragon 865 series")
        put("KALAMA", "Snapdragon 8 Gen 2")
        put("PINEAPPLE", "Snapdragon 8 Gen 3")
        put("LAHAINA", "Snapdragon 888 series")
        put("MSMNILE", "Snapdragon 855 series")
        put("WAIPIO", "Snapdragon 8 Gen 1")
        put("TARO", "Snapdragon 8 Gen 1")
        put("CAPE", "Snapdragon 8+ Gen 1")
        put("KONA", "Snapdragon 865 series")
        put("SC8380XP", "Snapdragon X Elite")
        put("SC8280XP", "Snapdragon 8cx Gen 3")
        put("SC8180X", "Snapdragon 8cx")
        put("SC7280", "Snapdragon 7c+ Gen 3")
        put("SC7180", "Snapdragon 7c")
        put("QM215", "Snapdragon 215")

        // MediaTek Dimensity.
        put("MT6993", "Dimensity 9500")
        put("MT6991", "Dimensity 9400 series")
        put("MT6989", "Dimensity 9300 series")
        put("MT6985", "Dimensity 9200 series")
        put("MT6983", "Dimensity 9000 series")
        put("MT6899", "Dimensity 8400 series")
        put("MT6897", "Dimensity 8300 series")
        put("MT6896", "Dimensity 8200")
        put("MT6895", "Dimensity 8000/8100")
        put("MT6893", "Dimensity 1200/1300")
        put("MT6891", "Dimensity 1100")
        put("MT6889", "Dimensity 1000+")
        put("MT6886", "Dimensity 7200 series")
        put("MT6885", "Dimensity 1000 series")
        put("MT6878", "Dimensity 7300 series")
        put("MT6877", "Dimensity 900/920/1080")
        put("MT6875", "Dimensity 820")
        put("MT6873", "Dimensity 800 series")
        put("MT6855", "Dimensity 930")
        put("MT6853", "Dimensity 720 series")
        put("MT6835", "Dimensity 6100+/6300")
        put("MT6833", "Dimensity 700/810")

        // MediaTek Helio.
        put("MT8781", "Helio G99")
        put("MT6789", "Helio G99/G100")
        put("MT6781", "Helio G96")
        put("MT6785", "Helio G90/G95")
        put("MT6779", "Helio P90/G90")
        put("MT6771", "Helio P60/P70")
        put("MT6769", "Helio G70/G80/G85/G88")
        put("MT6768", "Helio G80/G85")
        put("MT6765", "Helio P35/G35/G37")
        put("MT6763", "Helio P23/P30")
        put("MT6762", "Helio P22/G25")
        put("MT6757", "Helio P20/P25")
        put("MT6755", "Helio P10")
        put("MT6799", "Helio X30")
        put("MT6797", "Helio X20 series")
        put("MT6758", "Helio P30")
        put("MT6739", "MediaTek MT6739")
        put("MT6738", "MediaTek MT6738")
        put("MT6737", "MediaTek MT6737")
        put("MT6735", "MediaTek MT6735")
        put("MT6732", "MediaTek MT6732")
        put("MT6753", "MediaTek MT6753")
        put("MT6752", "MediaTek MT6752")
        put("MT6750", "MediaTek MT6750")
        put("MT6595", "MediaTek MT6595")
        put("MT6592", "MediaTek MT6592")
        put("MT6589", "MediaTek MT6589")
        put("MT6582", "MediaTek MT6582")
        put("MT6577", "MediaTek MT6577")
        put("MT6575", "MediaTek MT6575")
        put("MT6572", "MediaTek MT6572")
        put("MT8183", "MediaTek MT8183")
        put("MT8176", "MediaTek MT8176")
        put("MT8173", "MediaTek MT8173")

        // Samsung Exynos platform identifiers.
        put("S5E9965", "Exynos 2600")
        put("S5E9955", "Exynos 2500")
        put("S5E9945", "Exynos 2400")
        put("S5E9925", "Exynos 2200")
        put("S5E9840", "Exynos 2100")
        put("S5E9830", "Exynos 990")
        put("S5E9825", "Exynos 9825")
        put("S5E9820", "Exynos 9820")
        put("S5E9815", "Exynos 1080")
        put("S5E9810", "Exynos 9810")
        put("S5E9630", "Exynos 980")
        put("S5E9611", "Exynos 9611")
        put("S5E9610", "Exynos 9610")
        put("S5E9609", "Exynos 9609")
        put("S5E8895", "Exynos 8895")
        put("S5E8855", "Exynos 1580")
        put("S5E8845", "Exynos 1480")
        put("S5E8835", "Exynos 1380")
        put("S5E8825", "Exynos 1280")
        put("S5E8535", "Exynos 1330")
        put("S5E3830", "Exynos 850")
        put("S5E7904", "Exynos 7904")
        put("S5E7885", "Exynos 7885")
        put("S5E7880", "Exynos 7880")
        put("S5E7872", "Exynos 7872")
        put("S5E7870", "Exynos 7870")
        put("S5E7580", "Exynos 7580")
        put("S5E7570", "Exynos 7570")
        put("S5E8890", "Exynos 8890")
        put("S5E7420", "Exynos 7420")
        put("S5E5433", "Exynos 5433")
        put("S5E5422", "Exynos 5422")
        put("S5E5420", "Exynos 5420")
        put("S5E5410", "Exynos 5410")
        put("S5E5250", "Exynos 5250")
        put("S5E4412", "Exynos 4412")
        put("S5E4212", "Exynos 4212")
        put("S5E4210", "Exynos 4210")

        // Google Tensor platform and board identifiers.
        put("GS501", "Google Tensor G5")
        put("LAGUNA", "Google Tensor G5")
        put("GS401", "Google Tensor G4")
        put("ZUMAPRO", "Google Tensor G4")
        put("GS301", "Google Tensor G3")
        put("ZUMA", "Google Tensor G3")
        put("GS201", "Google Tensor G2")
        put("GS101", "Google Tensor")

        // HiSilicon Kirin platform identifiers.
        put("HI36A0", "Kirin 9000S")
        put("HI3690", "Kirin 990 series")
        put("HI3680", "Kirin 980")
        put("HI3670", "Kirin 970")
        put("HI3660", "Kirin 960")
        put("HI3650", "Kirin 950/955")
        put("HI3635", "Kirin 935")
        put("HI6280", "Kirin 820")
        put("HI6260", "Kirin 810")
        put("HI6250", "Kirin 650/710 series")
        put("K3V2", "Kirin K3V2")

        // Unisoc/Spreadtrum identifiers normally exposed without a vendor name.
        put("T9100", "Unisoc T9100")
        put("T8300", "Unisoc T8300")
        put("T820", "Unisoc T820")
        put("T810", "Unisoc T810")
        put("T770", "Unisoc T770")
        put("T765", "Unisoc T765")
        put("T760", "Unisoc T760")
        put("T750", "Unisoc T750")
        put("T7300", "Unisoc T7300")
        put("T7280", "Unisoc T7280")
        put("T7250", "Unisoc T7250")
        put("T710", "Unisoc T710")
        put("T700", "Unisoc T700")
        put("T620", "Unisoc T620")
        put("T618", "Unisoc T618")
        put("T616", "Unisoc T616")
        put("T612", "Unisoc T612")
        put("T610", "Unisoc T610")
        put("T606", "Unisoc T606")
        put("T603", "Unisoc T603")
        put("T310", "Unisoc T310")
        put("SC9863A", "Unisoc SC9863A")
        put("SC9863", "Unisoc SC9863")
        put("SC9860", "Spreadtrum SC9860")
        put("SC9850", "Spreadtrum SC9850")
        put("SC9832E", "Unisoc SC9832E")
        put("SC9830", "Spreadtrum SC9830")
        put("SC8830", "Spreadtrum SC8830")
        put("SC7731", "Spreadtrum SC7731")
        put("UMS9620", "Unisoc T820")
        put("UMS9230", "Unisoc T606/T612")
        put("UMS512", "Unisoc T610/T618")

        // Common tablet, handheld and TV SoCs.
        put("RK3588", "Rockchip RK3588")
        put("RK3576", "Rockchip RK3576")
        put("RK3568", "Rockchip RK3568")
        put("RK3566", "Rockchip RK3566")
        put("RK3562", "Rockchip RK3562")
        put("RK3528", "Rockchip RK3528")
        put("RK3399", "Rockchip RK3399")
        put("RK3368", "Rockchip RK3368")
        put("RK3328", "Rockchip RK3328")
        put("RK3326", "Rockchip RK3326")
        put("RK3288", "Rockchip RK3288")
        put("RK3229", "Rockchip RK3229")
        put("RK3188", "Rockchip RK3188")
        put("RK3128", "Rockchip RK3128")
        put("RK3126", "Rockchip RK3126")
        put("ALLWINNERA733", "Allwinner A733")
        put("ALLWINNERA523", "Allwinner A523")
        put("ALLWINNERA133", "Allwinner A133")
        put("ALLWINNERA100", "Allwinner A100")
        put("ALLWINNERA64", "Allwinner A64")
        put("ALLWINNERA33", "Allwinner A33")
        put("ALLWINNERA31", "Allwinner A31")
        put("ALLWINNERA23", "Allwinner A23")
        put("ALLWINNERA20", "Allwinner A20")
        put("ALLWINNERA13", "Allwinner A13")
        put("ALLWINNERA10", "Allwinner A10")
        // Allwinner BSP platform identifiers can cover multiple retail SoCs. Keep shared
        // identifiers honest and let an explicit H313/H616/H618/etc. token found in any
        // Android build field take precedence in resolve().
        put("SUN55IW6", "Allwinner T536 family")
        put("SUN55IW3", "Allwinner A523/A527/T527/H728 family")
        put("SUN50IW12", "Allwinner H713/TV303 family")
        put("SUN50IW11", "Allwinner R329")
        put("SUN50IW10", "Allwinner A100/A133 family")
        put("SUN50IW9", "Allwinner H313/H616/H618/H700 family")
        put("SUN50IW6", "Allwinner H6")
        put("SUN50IW3", "Allwinner A63")
        put("SUN50IW2", "Allwinner H5")
        put("SUN50IW1", "Allwinner A64/H64 family")
        put("SUN20IW1", "Allwinner D1/F133 family")
        put("SUN8IW7", "Allwinner H2+/H3 family")
        put("S928X", "Amlogic S928X")
        put("A311D", "Amlogic A311D")
        put("S922X", "Amlogic S922X")
        put("S905X4", "Amlogic S905X4")
        put("S905X3", "Amlogic S905X3")
        put("S905X2", "Amlogic S905X2")
        put("S905W", "Amlogic S905W")
        put("S905", "Amlogic S905")
        put("S912", "Amlogic S912")
        put("T210", "NVIDIA Tegra X1")
        put("T124", "NVIDIA Tegra K1")
        put("T114", "NVIDIA Tegra 4")
        put("TEGRA3", "NVIDIA Tegra 3")
    }

    private val aliasesByLength = socNames.keys.sortedByDescending(String::length)

    private val explicitAllwinnerSocNames: Map<String, String> = mapOf(
        "H728" to "Allwinner H728",
        "H713" to "Allwinner H713",
        "H700" to "Allwinner H700",
        "H618" to "Allwinner H618",
        "H616" to "Allwinner H616",
        "H313" to "Allwinner H313",
        "A733" to "Allwinner A733",
        "A527" to "Allwinner A527",
        "A523" to "Allwinner A523",
        "A133" to "Allwinner A133",
        "A100" to "Allwinner A100",
        "T536" to "Allwinner T536",
        "T527" to "Allwinner T527",
        "R329" to "Allwinner R329",
        "A64" to "Allwinner A64",
        "H6" to "Allwinner H6",
        "H5" to "Allwinner H5",
        "H3" to "Allwinner H3",
        "D1" to "Allwinner D1"
    )

    fun currentDeviceName(): String {
        val socModel = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) Build.SOC_MODEL else ""
        val socManufacturer = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) Build.SOC_MANUFACTURER else ""
        return resolve(
            socModel = socModel,
            hardware = Build.HARDWARE,
            board = Build.BOARD,
            manufacturer = socManufacturer,
            model = Build.MODEL,
            device = Build.DEVICE,
            product = Build.PRODUCT
        )
    }

    fun resolve(
        socModel: String?,
        hardware: String? = null,
        board: String? = null,
        manufacturer: String? = null,
        model: String? = null,
        device: String? = null,
        product: String? = null
    ): String {
        listOf(model, device, product)
            .mapNotNull { it?.trim()?.takeIf(String::isNotEmpty) }
            .forEach { candidate ->
                resolveHandheldModel(candidate)?.let { return it }
            }

        // TV boxes and handhelds frequently expose the exact SoC only in ro.product.device
        // or ro.product.name while ro.hardware contains an ambiguous Allwinner BSP family.
        val candidates = listOf(socModel, hardware, board, device, product, model)
            .mapNotNull { it?.trim()?.takeIf(String::isNotEmpty) }

        candidates.forEach { candidate ->
            resolveExplicitAllwinnerSoc(candidate)?.let { return it }
        }

        candidates.forEach { candidate ->
            resolveKnownCode(candidate)?.let { return it }
        }

        val readable = candidates.firstOrNull(::isReadableMarketingName)
        if (readable != null) return cleanDisplayName(readable)

        return inferVendorFamily(candidates, manufacturer) ?: "Unknown SoC"
    }

    internal fun resolveKnownCode(value: String): String? {
        val normalized = normalize(value)
        val alias = aliasesByLength.firstOrNull(normalized::contains) ?: return null
        return socNames.getValue(alias)
    }

    internal fun resolveHandheldModel(value: String): String? {
        val normalized = normalize(value)
        return handheldModelNames[normalized]
    }

    internal fun resolveExplicitAllwinnerSoc(value: String): String? {
        val uppercase = value.uppercase(Locale.US)
        val normalized = normalize(value)
        return explicitAllwinnerSocNames.entries.firstOrNull { (alias, _) ->
            val separatedToken = Regex("(^|[^A-Z0-9])${Regex.escape(alias)}([^A-Z0-9]|$)")
                .containsMatchIn(uppercase)
            val vendorToken = normalized.contains("ALLWINNER$alias") || normalized.contains("ALLWINER$alias")
            val sunxiToken = normalized.startsWith("SUN") && normalized.endsWith(alias)
            separatedToken || vendorToken || sunxiToken
        }?.value
    }

    private fun normalize(value: String): String = value
        .uppercase(Locale.US)
        .filter(Char::isLetterOrDigit)

    private fun isReadableMarketingName(value: String): Boolean {
        val normalized = value.lowercase(Locale.US)
        return listOf(
            "snapdragon", "dimensity", "helio", "exynos", "tensor", "kirin",
            "unisoc", "spreadtrum", "rockchip", "allwinner", "allwiner", "amlogic", "tegra"
        ).any(normalized::contains)
    }

    private fun inferVendorFamily(candidates: List<String>, manufacturer: String?): String? {
        val identifiers = candidates.map(::normalize)
        val vendor = normalize(manufacturer.orEmpty())

        return when {
            identifiers.any { it.startsWith("SM") || it.startsWith("MSM") || it.startsWith("SDM") || it.startsWith("APQ") || it.startsWith("QM") } ||
                vendor.contains("QUALCOMM") -> "Snapdragon"
            identifiers.any { it.startsWith("MT") } || vendor.contains("MEDIATEK") -> "MediaTek"
            identifiers.any { it.startsWith("S5E") || it.startsWith("UNIVERSAL") } || vendor.contains("SAMSUNG") -> "Samsung Exynos"
            identifiers.any { it.startsWith("GS") || it.contains("ZUMA") || it.contains("LAGUNA") } || vendor.contains("GOOGLE") -> "Google Tensor"
            identifiers.any { it.startsWith("HI") || it.startsWith("KIRIN") } || vendor.contains("HISILICON") -> "Kirin"
            identifiers.any { it.startsWith("UMS") } || vendor.contains("UNISOC") || vendor.contains("SPREADTRUM") -> "Unisoc"
            identifiers.any { it.startsWith("RK") } || vendor.contains("ROCKCHIP") -> "Rockchip"
            identifiers.any { it.startsWith("SUN") || it.startsWith("ALLWINNER") || it.startsWith("ALLWINER") } ||
                vendor.contains("ALLWINNER") || vendor.contains("ALLWINER") -> "Allwinner"
            vendor.contains("AMLOGIC") -> "Amlogic"
            identifiers.any { it.startsWith("TEGRA") } || vendor.contains("NVIDIA") -> "NVIDIA Tegra"
            else -> null
        }
    }

    private fun cleanDisplayName(value: String): String = value
        .replace("Qualcomm Technologies, Inc", "Qualcomm", ignoreCase = true)
        .replace("Allwiner", "Allwinner", ignoreCase = true)
        .replace("(TM)", "", ignoreCase = true)
        .replace(Regex("\\s+"), " ")
        .trim()

}
