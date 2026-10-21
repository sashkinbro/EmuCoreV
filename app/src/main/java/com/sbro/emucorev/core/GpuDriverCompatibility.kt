package com.sbro.emucorev.core

import java.util.Locale

data class SnapdragonGpuProfile(
    val socName: String,
    val adrenoName: String,
    val family: AdrenoFamily
)

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

    fun match(driver: RemoteGpuDriver, profile: SnapdragonGpuProfile?): GpuDriverMatch {
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
