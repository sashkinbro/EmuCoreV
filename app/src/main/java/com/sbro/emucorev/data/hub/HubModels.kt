package com.sbro.emucorev.data.hub

import kotlinx.serialization.Serializable
import kotlinx.serialization.json.JsonObject

enum class HubKind(val wireValue: String) {
    NEWS("news"),
    VIDEOS("videos"),
    HISTORY("history"),
    MANUALS("manuals");

    companion object {
        fun fromWire(value: String): HubKind? = entries.firstOrNull { it.wireValue == value }
    }
}

@Serializable
data class HubFileReference(
    val path: String? = null,
    val url: String? = null,
    val sha256: String,
    val bytes: Long,
    val contentType: String,
    val itemCount: Int? = null
)

@Serializable
data class HubChannel(
    val formatVersion: Int,
    val channel: String,
    val releaseId: String,
    val catalogRevision: Long,
    val commit: String,
    val issuedAt: String,
    val minimumClientVersionCode: Int,
    val manifest: HubFileReference
)

@Serializable
data class HubManifest(
    val formatVersion: Int,
    val schemaRevision: Int,
    val releaseId: String,
    val catalogRevision: Long,
    val generatedAt: String,
    val defaultLocale: String,
    val supportedLocales: List<String>,
    val localeFallbacks: Map<String, List<String>>,
    val minimumClientVersionCode: Int,
    val counts: Map<String, Int>,
    val indexes: Map<String, Map<String, List<HubFileReference>>>,
    val assets: HubFileReference,
    val tombstones: HubFileReference,
    val themes: HubFileReference? = null,
    val store: HubFileReference? = null
)

@Serializable
data class HubIndexPage(
    val formatVersion: Int,
    val releaseId: String,
    val catalogRevision: Long,
    val kind: String,
    val locale: String,
    val page: Int,
    val next: HubFileReference? = null,
    val items: List<HubIndexItem>
)

@Serializable
data class HubIndexItem(
    val id: String,
    val kind: String,
    val contentVersion: Int,
    val localeVersion: Int,
    val title: String,
    val summary: String,
    val publishedAt: String,
    val updatedAt: String,
    val categoryIds: List<String>,
    val tagIds: List<String>,
    val relatedProductIds: List<String> = emptyList(),
    val featured: Boolean = false,
    val priority: Int = 0,
    val canonicalUrl: String,
    val heroAssetId: String? = null,
    val document: HubFileReference,
    val sourceCount: Int,
    val sources: List<HubSource> = emptyList(),
    val eventDate: String? = null,
    val datePrecision: String? = null,
    val year: Int? = null,
    val provider: String? = null,
    val providerId: String? = null,
    val channelTitle: String? = null,
    val sourceLanguage: String? = null,
    val thumbnailUrl: String? = null
)

@Serializable
data class HubSource(
    val id: String? = null,
    val title: String,
    val publisher: String,
    val url: String,
    val publishedAt: String? = null
)

@Serializable
data class HubAssetCatalog(
    val formatVersion: Int,
    val releaseId: String,
    val assets: List<HubAsset>
)

@Serializable
data class HubAsset(
    val assetId: String,
    val kind: String,
    val variants: Map<String, HubAssetVariant>,
    val rights: HubAssetRights,
    val ownerContentId: String
)

@Serializable
data class HubAssetVariant(
    val path: String,
    val sha256: String,
    val bytes: Long,
    val contentType: String,
    val width: Int,
    val height: Int
)

@Serializable
data class HubAssetRights(
    val creator: String,
    val sourceUrl: String,
    val licenseId: String,
    val licenseUrl: String? = null,
    val attribution: String? = null,
    val modified: Boolean = false,
    val downloadAllowed: Boolean = false
)

@Serializable
data class HubLocalizedDocument(
    val schemaVersion: Int,
    val id: String,
    val locale: String,
    val basedOnVersion: Int,
    val localeVersion: Int,
    val status: String,
    val title: String,
    val summary: String,
    val author: String,
    val heroAlt: String? = null,
    val searchKeywords: List<String> = emptyList(),
    val categoryLabels: Map<String, String> = emptyMap(),
    val blocks: List<JsonObject> = emptyList(),
    val sourceLabels: Map<String, String> = emptyMap()
)

data class HubItem(
    val id: String,
    val kind: HubKind,
    val title: String,
    val summary: String,
    val publishedAt: String,
    val updatedAt: String,
    val eventDate: String?,
    val datePrecision: String?,
    val year: Int?,
    val categories: List<String>,
    val tags: List<String>,
    val relatedProductIds: List<String>,
    val featured: Boolean,
    val priority: Int,
    val canonicalUrl: String,
    val sources: List<HubSource>,
    val heroThumbnailUrl: String?,
    val heroThumbnailSha256: String?,
    val heroThumbnailBytes: Long?,
    val heroDisplayUrl: String?,
    val heroDisplaySha256: String?,
    val heroDisplayBytes: Long?,
    val heroOriginalUrl: String?,
    val heroOriginalSha256: String?,
    val heroOriginalBytes: Long?,
    val imageDownloadAllowed: Boolean,
    val heroAttribution: String?,
    val heroSourceUrl: String?,
    val heroLicenseId: String?,
    val heroLicenseUrl: String?,
    val providerId: String?,
    val channelTitle: String?,
    val sourceLanguage: String?,
    val isFavorite: Boolean
)

data class HubArticle(
    val item: HubItem,
    val document: HubLocalizedDocument,
    val assets: Map<String, HubAssetEntity>,
    val fromCache: Boolean
)

data class HubSyncResult(
    val changed: Boolean,
    val usedLocale: String,
    val fallbackLocale: String? = null,
    val warning: Throwable? = null
)
