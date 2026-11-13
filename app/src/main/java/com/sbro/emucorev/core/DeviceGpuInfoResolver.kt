package com.sbro.emucorev.core

import java.util.Locale

/** Evidence identifying [GpuModelInfo], rather than merely the origin of a raw string. */
enum class GpuInfoSource {
    GL_RENDERER,
    NATIVE_RENDERER,
    SOC_CATALOG,
    UNKNOWN
}

/**
 * Device hardware identity; a catalog fallback does not identify the active engine renderer.
 * [source] identifies the base model; a matching SoC may supply a missing Mali MC/MP suffix.
 */
data class DeviceGpuSnapshot(
    val model: GpuModelInfo?,
    val renderer: String?,
    val source: GpuInfoSource
)

/** Active native renderer evidence, kept separate from inferred device hardware. */
data class ActiveGpuSnapshot(
    val model: GpuModelInfo?,
    val renderer: String?,
    val source: GpuInfoSource
)

/** Pure policy only: callers acquire renderer/SoC evidence outside this helper. */
internal object DeviceGpuInfoResolver {
    private val CORE_COUNT = Regex("""\s+((?:mc|mp)\s*[1-9]\d?)$""", RegexOption.IGNORE_CASE)

    fun resolve(
        renderer: String?,
        socName: String?,
        catalog: (String?) -> GpuModelInfo? = SocGpuCatalog::forSoc
    ): DeviceGpuSnapshot {
        val parsed = GpuModelParser.parse(renderer)
        val fromCatalog = catalog(socName)
        val model = parsed?.let { mergeCoreCount(it, fromCatalog) } ?: fromCatalog
        val source = when {
            parsed != null -> GpuInfoSource.GL_RENDERER
            fromCatalog != null -> GpuInfoSource.SOC_CATALOG
            else -> GpuInfoSource.UNKNOWN
        }
        return DeviceGpuSnapshot(model, renderer, source)
    }

    /** An unknown/software native renderer remains unknown even on a known physical device. */
    fun resolveActiveRenderer(renderer: String?): ActiveGpuSnapshot {
        val parsed = GpuModelParser.parse(renderer)
        return ActiveGpuSnapshot(
            model = parsed,
            renderer = renderer,
            source = if (parsed != null) GpuInfoSource.NATIVE_RENDERER else GpuInfoSource.UNKNOWN
        )
    }

    private fun mergeCoreCount(parsed: GpuModelInfo, catalog: GpuModelInfo?): GpuModelInfo {
        if (parsed.vendor != GpuVendor.MALI || catalog?.vendor != GpuVendor.MALI) return parsed
        if (CORE_COUNT.containsMatchIn(parsed.displayName)) return parsed
        val suffix = CORE_COUNT.find(catalog.displayName) ?: return parsed
        val catalogBase = catalog.displayName.substring(0, suffix.range.first).trim()
        if (!parsed.displayName.equals(catalogBase, ignoreCase = true)) return parsed
        val count = suffix.groupValues[1].replace(Regex("""\s+"""), "").uppercase(Locale.US)
        return parsed.copy(displayName = "${parsed.displayName} $count")
    }
}
