package com.sbro.emucorev.ui.common

import com.sbro.emucorev.data.ProfileDeviceInfoProvider

private val coreBrandPrefix = Regex("^(?:ECV|Vita3K)(?=\\s|$)", RegexOption.IGNORE_CASE)
private val legacyCorePrefix = Regex("^Vita3K(?=\\s|$)", RegexOption.IGNORE_CASE)
private val upstreamMetadata = Regex("\\s*\\((?:based on|upstream)\\s+(Vita3K(?:\\s+[^()]*)?)\\)", RegexOption.IGNORE_CASE)

/**
 * Presentation only. Native ECV telemetry owns its version/build suffix; legacy upstream or
 * missing telemetry uses the parent-owned canonical core metadata, never the Android app version.
 * Upstream attribution is displayed separately in About. Persisted compatibility IDs are untouched.
 * The default is a const val, so using it does not initialize the Android device-info provider.
 */
fun formatCoreDisplayName(
    nativeCore: String?,
    canonicalCore: String = ProfileDeviceInfoProvider.CORE_VERSION
): String {
    val raw = nativeCore.orEmpty().trim()
    val canonical = upstreamMetadata.replace(canonicalCore, "").trim().ifEmpty { "ECV" }
    if (raw.isEmpty() || legacyCorePrefix.containsMatchIn(raw) || raw.equals("ECV", ignoreCase = true)) {
        return canonical
    }
    val version = coreBrandPrefix.replaceFirst(upstreamMetadata.replace(raw, ""), "").trim()
    return if (version.isEmpty()) canonical else "ECV $version"
}

/** Upstream provenance is separate from the modified core version and persisted core identity. */
fun upstreamCoreDisplayName(
    upstreamCore: String = ProfileDeviceInfoProvider.UPSTREAM_CORE_VERSION
): String? {
    val raw = upstreamCore.trim()
    return upstreamMetadata.find(raw)?.groupValues?.get(1)?.trim()
        ?: raw.takeIf { legacyCorePrefix.containsMatchIn(it) }
}
