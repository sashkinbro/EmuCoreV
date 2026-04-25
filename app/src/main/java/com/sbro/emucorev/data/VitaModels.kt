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

enum class VitaCompatibilityState {
    UNKNOWN,
    NOTHING,
    BOOTABLE,
    INTRO,
    MENU,
    INGAME_LESS,
    INGAME_MORE,
    PLAYABLE
}

data class VitaCompatibilitySummary(
    val matchedTitleId: String,
    val issueId: Int,
    val state: VitaCompatibilityState,
    val updatedAtEpochSeconds: Long?,
    val candidateTitleIds: List<String> = listOf(matchedTitleId)
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
    val compatibility: VitaCompatibilitySummary? = null
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
    val compatibility: VitaCompatibilitySummary? = null
)
