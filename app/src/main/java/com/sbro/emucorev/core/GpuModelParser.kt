package com.sbro.emucorev.core

import java.util.Locale

/** Resolves Vulkan/OpenGL renderer strings to the short hardware names used by EmuCoreX. */
internal fun parseGpuModelName(renderer: String?): String? {
    val raw = renderer?.trim().orEmpty()
    if (raw.isEmpty()) return null
    val normalized = raw
        .replace("(TM)", " ", ignoreCase = true)
        .replace("(R)", " ", ignoreCase = true)
    val lower = normalized.lowercase(Locale.US)
    if (GPU_EMULATOR_MARKERS.any(lower::contains)) return null
    return parseAdreno(lower)
        ?: parseMali(lower)
        ?: parseXclipse(lower)
        ?: parsePowerVr(lower)
}

private val GPU_EMULATOR_MARKERS = listOf(
    "swiftshader", "llvmpipe", "android emulator", "goldfish", "virtualbox", "vmware", "qemu"
)
private val GPU_ADRENO = Regex("""adreno[\s\-]*(?:\(tm\)\s*)?(\d{3})""", RegexOption.IGNORE_CASE)
private val GPU_IMMORTALIS = Regex(
    """immortalis[\s\-]*g\s*(\d{3})(?:\s*((?:mc|mp)\s*\d{1,2}))?""", RegexOption.IGNORE_CASE
)
private val GPU_MALI = Regex(
    """mali[\s\-]*(g|t)?\s*(\d{2,4})(?:\s*((?:mc|mp)\s*\d{1,2}))?""", RegexOption.IGNORE_CASE
)
private val GPU_XCLIPSE = Regex("""xclipse[\s\-]*(\d{3})""", RegexOption.IGNORE_CASE)
private val GPU_POWERVR_DXT = Regex("""dxt[\s\-]*(\d{2,5}(?:[\s\-]+\d{2,5})*)""", RegexOption.IGNORE_CASE)
private val GPU_POWERVR_GE = Regex("""\bge[\s\-]*(\d{4})""", RegexOption.IGNORE_CASE)

private fun parseAdreno(text: String): String? = GPU_ADRENO.find(text)
    ?.groupValues?.get(1)?.let { "Adreno $it" }

private fun parseMali(text: String): String? {
    GPU_IMMORTALIS.find(text)?.let { match ->
        return "Immortalis-G${match.groupValues[1]}${gpuCoreSuffix(match.groupValues[2])}"
    }
    val match = GPU_MALI.find(text) ?: return null
    val prefix = when (match.groupValues[1]) {
        "g" -> "Mali-G"
        "t" -> "Mali-T"
        else -> "Mali-"
    }
    return prefix + match.groupValues[2] + gpuCoreSuffix(match.groupValues[3])
}

private fun parseXclipse(text: String): String? = GPU_XCLIPSE.find(text)
    ?.groupValues?.get(1)?.let { "Xclipse $it" }

private fun parsePowerVr(text: String): String? {
    if (!text.contains("powervr") && !text.contains("imgtec") && !text.contains("imagination")) return null
    GPU_POWERVR_DXT.find(text)?.let { match ->
        val id = match.groupValues[1].replace(Regex("\\s+"), "-")
        return "PowerVR DXT-$id"
    }
    GPU_POWERVR_GE.find(text)?.let { return "PowerVR GE${it.groupValues[1]}" }
    return "PowerVR"
}

private fun gpuCoreSuffix(raw: String): String {
    val cleaned = raw.replace(Regex("\\s+"), "").uppercase(Locale.US)
    return if (cleaned.isEmpty()) "" else " $cleaned"
}
