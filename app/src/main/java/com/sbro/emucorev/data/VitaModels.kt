package com.sbro.emucorev.data

data class InstalledVitaGame(
    val titleId: String,
    val title: String,
    val contentId: String?,
    val saveDataId: String?,
    val version: String?,
    val category: String?,
    val iconPath: String?,
    val installPath: String
)

data class VitaCatalogEntry(
    val igdbId: Long,
    val name: String,
    val year: Int?,
    val rating: Float?,
    val summary: String?,
    val coverUrl: String?,
    val heroUrl: String?,
    val genres: List<String> = emptyList(),
    val serials: List<String> = emptyList(),
    val compatibility: List<EcvCompatibilityUiRun> = emptyList()
)

data class VitaCatalogDetails(
    val igdbId: Long,
    val name: String,
    val year: Int?,
    val rating: Float?,
    val summary: String?,
    val coverUrl: String?,
    val heroUrl: String?,
    val genres: List<String>,
    val serials: List<String>,
    val screenshots: List<String>,
    val videos: List<String>,
    val compatibility: List<EcvCompatibilityUiRun> = emptyList()
)

enum class ProfileGameStatus {
    PLAYING,
    WANT_TO_PLAY,
    COMPLETED,
    DROPPED
}

data class ProfileGameListEntry(
    val igdbId: Long,
    val status: ProfileGameStatus?,
    val isFavorite: Boolean,
    val addedAtEpochMillis: Long,
    val updatedAtEpochMillis: Long
)

data class ProfileCatalogGame(
    val profile: ProfileGameListEntry,
    val catalog: VitaCatalogEntry
)
