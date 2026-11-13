package com.sbro.emucorev.ui.library

import com.sbro.emucorev.data.InstalledVitaGame
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext

internal const val STARTUP_COVER_WARM_LIMIT = 48

internal fun filterAndSortLibraryItems(
    allItems: List<InstalledVitaGame>,
    query: String,
    sortOption: LibrarySortOption,
    sortMetadata: Map<String, LibraryGameSortMetadata>
): List<InstalledVitaGame> {
    val normalizedQuery = query.trim()
    val filteredItems = allItems.filter {
        normalizedQuery.isBlank() ||
            it.title.contains(normalizedQuery, ignoreCase = true) ||
            it.titleId.contains(normalizedQuery, ignoreCase = true)
    }
    return sortInstalledGames(filteredItems, sortOption, sortMetadata)
}

internal fun startupVisibleTitleIds(
    allItems: List<InstalledVitaGame>,
    query: String,
    sortOption: LibrarySortOption,
    sortMetadata: Map<String, LibraryGameSortMetadata>
): List<String> = filterAndSortLibraryItems(allItems, query, sortOption, sortMetadata)
    .take(STARTUP_COVER_WARM_LIMIT)
    .map(InstalledVitaGame::titleId)

/** Runs only the injected managed-disk-cache warmup off-main, then allows first publication. */
internal suspend fun warmStartupCoverArtBeforePublish(
    titleIds: List<String>,
    artworkVersion: Long,
    dispatcher: CoroutineDispatcher = Dispatchers.IO,
    warmCached: suspend (titleIds: List<String>, artworkVersion: Long) -> Unit,
    publish: () -> Unit
) {
    if (titleIds.isNotEmpty()) {
        withContext(dispatcher) { warmCached(titleIds, artworkVersion) }
    }
    currentCoroutineContext().ensureActive()
    publish()
}
