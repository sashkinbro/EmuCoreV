package com.sbro.emucorev.core

import android.os.Build

object GpuHardwareProfiles {
    const val ADRENO = 0
    const val MALI = 1
    const val POWERVR = 2

    private var cachedDetectedProfile: Int? = null
    private var cachedMediaTekHardware: Boolean? = null

    fun normalize(profile: Int): Int = when (profile) {
        MALI -> MALI
        POWERVR -> POWERVR
        ADRENO -> ADRENO
        else -> MALI
    }

    fun isMediatekProfile(profile: Int): Boolean {
        return normalize(profile) == MALI || normalize(profile) == POWERVR
    }

    // Pass only the SoC-vendor hint. "mediatek" intentionally parses as an automatic GPU override:
    // the native renderer still uses GL_RENDERER/VkPhysicalDeviceProperties for the actual GPU,
    // which matters because older MediaTek generations can use PowerVR instead of Mali.
    fun coreOverrideFor(@Suppress("UNUSED_PARAMETER") profile: Int): String =
        if (isMediaTekHardware()) "mediatek" else "auto"

    fun isMediaTekHardware(): Boolean {
        cachedMediaTekHardware?.let { return it }
        return hasMediaTekSocHints(buildHardwareHints()).also { cachedMediaTekHardware = it }
    }

    fun detectHardwareProfile(): Int {
        cachedDetectedProfile?.let { return it }
        val profile = classifyHardwareProfile(buildHardwareHints())
        cachedDetectedProfile = profile
        return profile
    }

    private fun buildHardwareHints(): String {
        val socManufacturer = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            Build.SOC_MANUFACTURER
        } else { "" }
        val socModel = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            Build.SOC_MODEL
        } else { "" }
        val hardware = listOf(Build.HARDWARE, Build.BOARD, Build.DEVICE)
            .filterNot { it.isNullOrBlank() }
            .joinToString(" / ")
        val deviceModel = listOf(Build.MANUFACTURER, Build.MODEL)
            .filterNot { it.isNullOrBlank() }
            .joinToString(" ")
        val hints = listOf(socManufacturer, socModel, hardware, deviceModel)
            .joinToString(" ")
            .lowercase()
        return hints
    }

    internal fun hasMediaTekSocHints(rawHints: String): Boolean {
        val hints = rawHints.lowercase()
        return listOf("mediatek", "mtk", "dimensity", "helio").any(hints::contains) ||
            Regex("""\bmt\d{4}[a-z]*\b""").containsMatchIn(hints)
    }

    internal fun classifyHardwareProfile(rawHints: String): Int {
        val hints = rawHints.lowercase()
        return when {
            listOf("powervr", "imgtec", "imagination technologies").any(hints::contains) -> POWERVR
            listOf("qualcomm", "qcom", "snapdragon", "adreno").any(hints::contains) -> ADRENO
            hasMediaTekSocHints(hints) -> MALI
            listOf("samsung", "exynos", "xclipse").any(hints::contains) -> MALI
            listOf("mali", "immortalis", "arm").any(hints::contains) -> MALI
            else -> MALI
        }
    }
}
