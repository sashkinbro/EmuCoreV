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

    internal fun profileForSoc(socName: String): SnapdragonGpuProfile? {
        val normalized = socName.lowercase(Locale.US)
        if (!normalized.contains("snapdragon")) return null

        val mapping = SOC_GPU_MAPPINGS.firstOrNull { (socTokens, _) ->
            socTokens.any(normalized::contains)
        }?.second ?: return null
        return SnapdragonGpuProfile(
            socName = socName,
            adrenoName = "Adreno ${mapping.first}",
            family = mapping.second
        )
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

    private val SOC_GPU_MAPPINGS = listOf(
        listOf("8 elite gen 5") to ("840" to AdrenoFamily.A8XX),
        listOf("8 gen 5") to ("829" to AdrenoFamily.A8XX),
        listOf("8 elite") to ("830" to AdrenoFamily.A8XX),
        listOf("8s gen 4") to ("825" to AdrenoFamily.A8XX),
        listOf("8 gen 3") to ("750" to AdrenoFamily.A7XX),
        listOf("8s gen 3") to ("735" to AdrenoFamily.A7XX),
        listOf("8 gen 2") to ("740" to AdrenoFamily.A7XX),
        listOf("8+ gen 1", "8 gen 1") to ("730" to AdrenoFamily.A7XX),
        listOf("7 gen 4") to ("722" to AdrenoFamily.A7XX),
        listOf("7+ gen 3") to ("732" to AdrenoFamily.A7XX),
        listOf("7s gen 3") to ("710" to AdrenoFamily.A7XX),
        listOf("7 gen 3") to ("720" to AdrenoFamily.A7XX),
        listOf("7+ gen 2") to ("725" to AdrenoFamily.A7XX),
        listOf("7s gen 2") to ("710" to AdrenoFamily.A7XX),
        listOf("6 gen 4") to ("810" to AdrenoFamily.A8XX),
        listOf("6 gen 3", "6 gen 1") to ("710" to AdrenoFamily.A7XX),
        listOf("888") to ("660" to AdrenoFamily.A6XX),
        listOf("865") to ("650" to AdrenoFamily.A6XX),
        listOf("855") to ("640" to AdrenoFamily.A6XX),
        listOf("845") to ("630" to AdrenoFamily.A6XX),
        listOf("7 gen 1") to ("644" to AdrenoFamily.A6XX),
        listOf("780g", "778g") to ("642" to AdrenoFamily.A6XX),
        listOf("765") to ("620" to AdrenoFamily.A6XX),
        listOf("750g") to ("619" to AdrenoFamily.A6XX),
        listOf("730", "720g") to ("618" to AdrenoFamily.A6XX),
        listOf("695", "690", "680", "665", "662") to ("6xx" to AdrenoFamily.A6XX)
    )

    private val EXPLICIT_ADRENO_MODEL = Regex("8xx\\s*\\(([^)]+)\\)", RegexOption.IGNORE_CASE)
}
