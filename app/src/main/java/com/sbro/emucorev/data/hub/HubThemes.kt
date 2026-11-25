package com.sbro.emucorev.data.hub

import kotlinx.serialization.Serializable

/**
 * `catalog/v1/themes.json`, referenced by the optional `manifest.themes` file reference.
 * Clients that predate themes ignore the unknown manifest field.
 */
@Serializable
data class HubThemeCatalog(
    val formatVersion: Int,
    val releaseId: String,
    val categories: List<HubThemeCategory> = emptyList(),
    val themes: List<HubThemeEntry> = emptyList()
)

@Serializable
data class HubThemeCategory(
    val id: String,
    val labels: Map<String, String> = emptyMap()
) {
    fun label(locale: String): String = labels.localized(locale) ?: id
}

@Serializable
data class HubThemeEntry(
    val id: String,
    val version: Int,
    val requiresPro: Boolean = false,
    val dark: Boolean = true,
    val minAppVersionCode: Int = 0,
    val featured: Boolean = false,
    val publishedAt: String = "",
    val categories: List<String> = emptyList(),
    val tags: List<String> = emptyList(),
    val name: Map<String, String> = emptyMap(),
    val description: Map<String, String> = emptyMap(),
    val swatch: List<String> = emptyList(),
    val preview: HubThemeFile,
    val packageFile: HubThemeFile
) {
    fun displayName(locale: String): String = name.localized(locale) ?: id
    fun displayDescription(locale: String): String = description.localized(locale).orEmpty()
}

@Serializable
data class HubThemeFile(
    val path: String,
    val sha256: String,
    val bytes: Long,
    val width: Int = 0,
    val height: Int = 0
)

/** Theme catalog resolved against the pinned commit base URL of the sync that produced it. */
data class HubThemeSnapshot(
    val baseUrl: String,
    val catalogRevision: Long,
    val catalog: HubThemeCatalog
) {
    fun url(file: HubThemeFile): String = baseUrl + file.path
}

internal fun Map<String, String>.localized(locale: String): String? {
    val language = locale.substringBefore('-').substringBefore('_').lowercase()
    return this[locale] ?: this[language] ?: this["en"] ?: values.firstOrNull()
}
