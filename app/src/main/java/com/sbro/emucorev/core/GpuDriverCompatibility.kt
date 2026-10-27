package com.sbro.emucorev.core

import android.os.Build
import java.io.File
import java.util.Locale

data class SnapdragonGpuProfile(
    val socName: String,
    val adrenoName: String,
    val family: AdrenoFamily
)

/** GPU vendor a driver package targets. */
enum class GpuDriverVendor(val displayName: String) {
    ADRENO("Adreno"),
    MALI("Mali"),
    XCLIPSE("Xclipse"),
    POWERVR("PowerVR"),
    UNKNOWN("Unknown");

    val supportsCustomDrivers: Boolean
        get() = this == ADRENO || this == MALI || this == XCLIPSE
}

/** SoC + GPU label used by the driver manager device card. */
data class GpuDriverDevice(
    val socName: String,
    val gpuName: String
)

/** Catalog driver package extension: which vendor the package targets. */
val RemoteGpuDriver.vendor: GpuDriverVendor
    get() = GpuDriverCompatibility.vendorOfCatalogGpu(gpu)

enum class AdrenoFamily(val catalogToken: String) {
    A6XX("6xx"),
    A7XX("7xx"),
    A8XX("8xx")
}

enum class GpuDriverMatch {
    COMPATIBLE,
    OTHER_FAMILY,
    UNKNOWN
}

/** Maps the detected Snapdragon SoC to the Adreno families used by driver packages. */
object GpuDriverRecommendations {
    fun currentDeviceProfile(): SnapdragonGpuProfile? =
        profileForSoc(MobileSocNameMapper.currentDeviceName())

    internal fun profileForSoc(socName: String): SnapdragonGpuProfile? =
        profileForGpu(socName, null)

    internal fun profileForGpu(socName: String, gpu: GpuModelInfo?): SnapdragonGpuProfile? {
        val model = SocGpuCatalog.forSoc(socName) ?: gpu ?: return null
        if (model.vendor != GpuVendor.ADRENO) return null
        val family = model.adrenoFamily ?: adrenoFamilyOf(model.displayName) ?: return null
        return SnapdragonGpuProfile(
            socName = socName,
            adrenoName = model.displayName,
            family = family
        )
    }

    private fun adrenoFamilyOf(modelName: String): AdrenoFamily? {
        val value = ADRENO_MODEL.find(modelName)?.groupValues?.getOrNull(1)?.toIntOrNull()
            ?: return null
        return when {
            value >= 800 -> AdrenoFamily.A8XX
            value >= 700 -> AdrenoFamily.A7XX
            value >= 600 -> AdrenoFamily.A6XX
            else -> null
        }
    }

    fun match(
        driver: RemoteGpuDriver,
        profile: SnapdragonGpuProfile?,
        deviceVendor: GpuDriverVendor? = null
    ): GpuDriverMatch {
        val driverVendor = driver.vendor
        if (driverVendor != GpuDriverVendor.UNKNOWN && driverVendor != GpuDriverVendor.ADRENO) {
            return when {
                deviceVendor == null -> GpuDriverMatch.UNKNOWN
                driverVendor == deviceVendor -> GpuDriverMatch.COMPATIBLE
                else -> GpuDriverMatch.OTHER_FAMILY
            }
        }
        if (driverVendor == GpuDriverVendor.ADRENO &&
            deviceVendor != null &&
            deviceVendor != GpuDriverVendor.ADRENO
        ) {
            return GpuDriverMatch.OTHER_FAMILY
        }

        profile ?: return GpuDriverMatch.UNKNOWN
        val supportedFamilies = supportedFamilies(driver.gpu)
        if (supportedFamilies.isEmpty()) return GpuDriverMatch.UNKNOWN
        if (profile.family !in supportedFamilies) return GpuDriverMatch.OTHER_FAMILY

        val explicitModels = EXPLICIT_ADRENO_MODEL.find(driver.gpu)
            ?.groupValues
            ?.getOrNull(1)
            ?.split('/', ',', ' ')
            ?.map(String::trim)
            ?.filter(String::isNotEmpty)
            .orEmpty()
        val deviceModel = profile.adrenoName.substringAfterLast(' ')
        return if (explicitModels.isNotEmpty() && deviceModel !in explicitModels) {
            GpuDriverMatch.OTHER_FAMILY
        } else {
            GpuDriverMatch.COMPATIBLE
        }
    }

    fun supportedFamilies(gpu: String): Set<AdrenoFamily> = buildSet {
        val normalized = gpu.lowercase(Locale.US)
        AdrenoFamily.entries.forEach { family ->
            if (family.catalogToken in normalized) add(family)
        }
    }

    private val EXPLICIT_ADRENO_MODEL = Regex("8xx\\s*\\(([^)]+)\\)", RegexOption.IGNORE_CASE)
    private val ADRENO_MODEL = Regex("""adreno\s*(\d{3})""", RegexOption.IGNORE_CASE)
}

/**
 * Device and driver package vendor detection for the custom GPU driver manager.
 *
 * Detection is deliberately split into pure functions so unit tests can drive
 * them with arbitrary build strings: [deviceVendor] for the device, and
 * [vendorOfCatalogGpu] / [vendorOfDriverLibrary] for packages. Cross-vendor
 * installs and selections are refused so an Adreno Turnip pack can never be
 * applied to a Mali device and a PanVK pack can never reach an Adreno device.
 */
object GpuDriverCompatibility {
    private val QUALCOMM_SIGNALS = listOf(
        "adreno",
        "qcom",
        "qualcomm",
        "qti",
        "snapdragon",
        "msm",
        "sdm",
        "sm8",
        "sm7",
        "sm6",
        "sm4",
        "qcs",
        "qcm",
        "kalama",
        "lahaina",
        "taro",
        "waipio"
    )

    private val XCLIPSE_SIGNALS = listOf(
        "xclipse",
        "exynos 1480",
        "exynos 1580",
        "exynos 1680",
        "exynos 2200",
        "exynos 2400",
        "exynos 2500",
        "exynos 2600",
        "s5e8845",
        "s5e8855",
        "s5e8865",
        "s5e9925",
        "s5e9945",
        "s5e9955",
        "s5e9965"
    )

    private val MALI_SIGNALS = listOf(
        "mediatek",
        "mtk",
        "dimensity",
        "helio",
        "exynos",
        "mali",
        "kirin",
        "hisilicon",
        "tensor",
        "unisoc",
        "spreadtrum",
        "comtech"
    )

    private val POWERVR_SIGNALS = listOf("powervr", "imgtec", "imagination")

    private val SNAPDRAGON_SOC_PATTERN = Regex("""\bsm[0-9]{3,4}[a-z]*\b""", RegexOption.IGNORE_CASE)
    private val EXYNOS_SOC_PATTERN = Regex("""\bs5e[0-9]{4}[a-z]*\b""", RegexOption.IGNORE_CASE)

    private const val DRIVER_VENDOR_METADATA = "driver_vendor.txt"
    private const val MALI_SX2_CATALOG_ID_PREFIX = "mali-sx2-"
    private const val MALI_SX2_LIBRARY_NAME = "libvulkan_malisx2.so"
    private val MALI_SX2_SUPPORTED_MODEL = Regex(
        """^(?:mali|immortalis)-g(?:52|57|68|77|78(?:ae)?|615|715)(?:mc\d{1,2}|mp\d{1,2})?$""",
        RegexOption.IGNORE_CASE
    )

    fun deviceVendor(): GpuDriverVendor = deviceVendor(currentDeviceInfo())

    fun vendorOfGpu(model: GpuModelInfo?): GpuDriverVendor = when (model?.vendor) {
        GpuVendor.ADRENO -> GpuDriverVendor.ADRENO
        GpuVendor.MALI -> GpuDriverVendor.MALI
        GpuVendor.XCLIPSE -> GpuDriverVendor.XCLIPSE
        GpuVendor.POWERVR -> GpuDriverVendor.POWERVR
        else -> GpuDriverVendor.UNKNOWN
    }

    internal fun deviceVendor(deviceInfo: String): GpuDriverVendor {
        val info = deviceInfo.lowercase(Locale.US)
        if (QUALCOMM_SIGNALS.any { it in info }) return GpuDriverVendor.ADRENO
        if (XCLIPSE_SIGNALS.any { it in info }) return GpuDriverVendor.XCLIPSE
        if (POWERVR_SIGNALS.any { it in info }) return GpuDriverVendor.POWERVR
        if (MALI_SIGNALS.any { it in info }) return GpuDriverVendor.MALI
        if (EXYNOS_SOC_PATTERN.containsMatchIn(info)) return GpuDriverVendor.MALI
        if (SNAPDRAGON_SOC_PATTERN.containsMatchIn(info)) return GpuDriverVendor.ADRENO
        return GpuDriverVendor.UNKNOWN
    }

    fun supportsCustomDrivers(): Boolean = deviceVendor().supportsCustomDrivers

    internal fun supportsCustomDrivers(deviceInfo: String): Boolean =
        deviceVendor(deviceInfo).supportsCustomDrivers

    fun supportsAdrenoToolsCustomDrivers(): Boolean = deviceVendor() == GpuDriverVendor.ADRENO

    internal fun supportsAdrenoToolsCustomDrivers(deviceInfo: String): Boolean =
        deviceVendor(deviceInfo) == GpuDriverVendor.ADRENO

    fun isCatalogDriverCompatible(
        driver: RemoteGpuDriver,
        deviceVendor: GpuDriverVendor = deviceVendor()
    ): Boolean = isVendorCompatible(driver.vendor, deviceVendor) &&
        (!driver.id.startsWith(MALI_SX2_CATALOG_ID_PREFIX, ignoreCase = true) || supportsMaliSx2Gpu())

    fun isInstalledDriverCompatible(
        mainLibraryName: String,
        driverVendor: GpuDriverVendor,
        deviceVendor: GpuDriverVendor = deviceVendor()
    ): Boolean = isVendorCompatible(driverVendor, deviceVendor) &&
        (!mainLibraryName.substringAfterLast('/').substringAfterLast('\\')
            .equals(MALI_SX2_LIBRARY_NAME, ignoreCase = true) || supportsMaliSx2Gpu())

    internal fun supportsMaliSx2Gpu(): Boolean {
        val socName = MobileSocNameMapper.currentDeviceName()
        val model = SocGpuCatalog.forSoc(socName) ?: return true
        if (model.vendor != GpuVendor.MALI) return false
        val normalized = model.displayName.lowercase(Locale.US).replace(" ", "")
        return MALI_SX2_SUPPORTED_MODEL.matches(normalized)
    }

    fun currentDevice(): GpuDriverDevice {
        val vendor = deviceVendor()
        val socName = MobileSocNameMapper.currentDeviceName()
            .takeIf { it.isNotBlank() }
            ?: socLabel().ifBlank { vendor.displayName }
        val catalogModel = SocGpuCatalog.forSoc(socName)
        val gpuName = catalogModel?.displayName ?: when (vendor) {
            GpuDriverVendor.ADRENO ->
                GpuDriverRecommendations.currentDeviceProfile()?.adrenoName ?: vendor.displayName
            else -> vendor.displayName
        }
        return GpuDriverDevice(socName = socName, gpuName = gpuName)
    }

    fun currentDeviceInfo(): String = buildList {
        add(Build.BOARD)
        add(Build.BRAND)
        add(Build.DEVICE)
        add(Build.HARDWARE)
        add(Build.MANUFACTURER)
        add(Build.MODEL)
        add(Build.PRODUCT)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            add(Build.SOC_MANUFACTURER)
            add(Build.SOC_MODEL)
        }
    }
        .joinToString(" ")

    fun vendorOfCatalogGpu(gpu: String): GpuDriverVendor {
        val normalized = gpu.lowercase(Locale.US)
        return when {
            "xclipse" in normalized || "exynos" in normalized -> GpuDriverVendor.XCLIPSE
            "mali" in normalized || "panvk" in normalized || "panfrost" in normalized -> GpuDriverVendor.MALI
            "adreno" in normalized || "freedreno" in normalized || "turnip" in normalized -> GpuDriverVendor.ADRENO
            "powervr" in normalized || "imgtec" in normalized || "imagination" in normalized -> GpuDriverVendor.POWERVR
            else -> GpuDriverVendor.UNKNOWN
        }
    }

    /**
     * Which vendor a driver library targets, read from markers inside the ELF.
     * The file name cannot be trusted: PanVK packs for Mali are named
     * `libvulkan_freedreno.so` so adrenotools-style loaders accept them.
     */
    fun vendorOfDriverLibrary(file: File): GpuDriverVendor {
        if (!file.isFile || file.length() <= 0L) return GpuDriverVendor.UNKNOWN
        // Xclipse first: Samsung's Vortek packs are their own stack and must
        // not be caught by a generic Mali marker later.
        if (file.containsAsciiMarker("xclipse") || file.containsAsciiMarker("vortek")) {
            return GpuDriverVendor.XCLIPSE
        }
        if (file.containsAsciiMarker("panvk") ||
            file.containsAsciiMarker("panfrost") ||
            file.containsAsciiMarker("libgles_mali") ||
            file.containsAsciiMarker("libmali") ||
            file.containsAsciiMarker("mali_kbase")
        ) {
            return GpuDriverVendor.MALI
        }
        if (file.containsAsciiMarker("freedreno") ||
            file.containsAsciiMarker("turnip") ||
            file.containsAsciiMarker("adreno")
        ) {
            return GpuDriverVendor.ADRENO
        }
        return GpuDriverVendor.UNKNOWN
    }

    fun isVendorCompatible(
        driverVendor: GpuDriverVendor,
        deviceVendor: GpuDriverVendor = deviceVendor()
    ): Boolean {
        if (driverVendor == GpuDriverVendor.UNKNOWN || deviceVendor == GpuDriverVendor.UNKNOWN) return true
        return driverVendor == deviceVendor
    }

    /** Reads the vendor recorded next to an installed library, if any. */
    fun installedDriverVendor(libraryFile: File): GpuDriverVendor {
        val metadata = libraryFile.parentFile?.let { File(it, DRIVER_VENDOR_METADATA) } ?: return GpuDriverVendor.UNKNOWN
        if (!metadata.isFile) return GpuDriverVendor.UNKNOWN
        val stored = runCatching { metadata.readText().trim() }.getOrNull().orEmpty()
        return GpuDriverVendor.entries.firstOrNull { it.name.equals(stored, ignoreCase = true) }
            ?: GpuDriverVendor.UNKNOWN
    }

    fun writeInstalledDriverVendor(libraryFile: File, vendor: GpuDriverVendor) {
        if (vendor == GpuDriverVendor.UNKNOWN) return
        runCatching {
            libraryFile.parentFile?.let { File(it, DRIVER_VENDOR_METADATA).writeText(vendor.name) }
        }
    }

    private fun socLabel(): String = runCatching {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) Build.SOC_MODEL.orEmpty() else Build.HARDWARE
    }.getOrDefault(Build.HARDWARE).orEmpty()
}

private fun File.containsAsciiMarker(marker: String): Boolean {
    if (marker.isEmpty()) return true
    val needle = marker.lowercase(Locale.US).toByteArray(Charsets.US_ASCII)
    if (needle.isEmpty()) return true
    return try {
        inputStream().buffered(MARKER_SCAN_CHUNK).use { input ->
            val buffer = ByteArray(MARKER_SCAN_CHUNK)
            var carry = ByteArray(0)
            while (true) {
                val read = input.read(buffer)
                if (read <= 0) break
                val haystack = ByteArray(carry.size + read)
                carry.copyInto(haystack)
                buffer.copyInto(haystack, carry.size, 0, read)
                if (haystack.containsAsciiMarker(needle)) return true
                val keep = minOf(needle.size - 1, haystack.size)
                carry = haystack.copyOfRange(haystack.size - keep, haystack.size)
            }
            false
        }
    } catch (_: Exception) {
        false
    }
}

private fun ByteArray.containsAsciiMarker(needle: ByteArray): Boolean {
    if (needle.isEmpty()) return true
    if (size < needle.size) return false
    val last = size - needle.size
    var index = 0
    while (index <= last) {
        var matched = 0
        while (matched < needle.size &&
            ((this[index + matched].toInt() and 0xFF) or 0x20) == needle[matched].toInt()
        ) {
            matched++
        }
        if (matched == needle.size) return true
        index++
    }
    return false
}

private const val MARKER_SCAN_CHUNK = 1 shl 20
