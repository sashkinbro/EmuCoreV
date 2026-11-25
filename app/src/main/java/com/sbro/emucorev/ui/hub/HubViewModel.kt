package com.sbro.emucorev.ui.hub

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.data.hub.HubItem
import com.sbro.emucorev.data.hub.HubKind
import com.sbro.emucorev.data.hub.HubLocaleResolver
import com.sbro.emucorev.data.hub.HubRepository
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import java.text.Normalizer
import java.util.Locale

/** Top-level mode of the Hub: the shelves feed or the Store. */
enum class HubMode {
    DISCOVER,
    STORE
}

/** Full list a Discover shelf opens with "See all" (null section = the shelves feed). */
enum class HubSection {
    NEWS,
    VIDEOS,
    HISTORY,
    MANUALS,
    SAVED
}

enum class HubSortOrder {
    NEWEST_FIRST,
    OLDEST_FIRST,
    TITLE_ASCENDING
}

enum class HubProductFilter {
    ALL,
    EMUCOREX,
    EMUCOREV,
    PLAYSTATION_2,
    OTHER_EMULATORS
}

data class HubUiState(
    val items: List<HubItem> = emptyList(),
    val mode: HubMode = HubMode.DISCOVER,
    val section: HubSection? = null,
    /** Mode to return to when the Saved list is closed. */
    val savedReturnMode: HubMode = HubMode.DISCOVER,
    val searchQuery: String = "",
    val sortOrder: HubSortOrder = HubSortOrder.NEWEST_FIRST,
    val productFilter: HubProductFilter = HubProductFilter.ALL,
    val featuredOnly: Boolean = false,
    val isInitialLoading: Boolean = true,
    val isRefreshing: Boolean = false,
    val showRefreshIndicator: Boolean = false,
    val isOffline: Boolean = false,
    val error: Throwable? = null,
    val fallbackLocale: String? = null
) {
    val hasActiveFilters: Boolean
        get() = productFilter != HubProductFilter.ALL || featuredOnly

    val visibleItems: List<HubItem>
        get() {
            var selected = when (section) {
                null -> items
                HubSection.NEWS -> items.filter { it.kind == HubKind.NEWS }
                HubSection.VIDEOS -> items.filter { it.kind == HubKind.VIDEOS }
                HubSection.HISTORY -> items.filter { it.kind == HubKind.HISTORY }
                HubSection.MANUALS -> items.filter { it.kind == HubKind.MANUALS }
                HubSection.SAVED -> items.filter(HubItem::isFavorite)
            }
            selected = selected.filter { it.matchesProductFilter(productFilter) }
            if (featuredOnly) selected = selected.filter(HubItem::featured)

            val normalizedQuery = searchQuery.normalizedSearchText()
            if (normalizedQuery.isNotBlank()) {
                selected = selected.filter { item ->
                    buildString {
                        append(item.title)
                        append(' ')
                        append(item.summary)
                        append(' ')
                        append(item.categories.joinToString(" "))
                        append(' ')
                        append(item.tags.joinToString(" "))
                        append(' ')
                        append(item.channelTitle.orEmpty())
                        append(' ')
                        append(item.year?.toString().orEmpty())
                    }.normalizedSearchText().contains(normalizedQuery)
                }
            }
            return selected.sortedWith(sortOrder.comparator())
        }
}

class HubViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = HubRepository.get(application)
    private val locale = HubLocaleResolver.current() // the app language (AppLocaleManager sets the default), not the device one
    private val _uiState = MutableStateFlow(HubUiState())
    val uiState: StateFlow<HubUiState> = _uiState.asStateFlow()
    private var itemsJob: Job? = null
    var hasShownInitialSkeleton: Boolean = false
        private set

    init {
        observeItems()
        refresh(force = false, showIndicator = false)
    }

    fun selectMode(mode: HubMode) {
        _uiState.update {
            if (it.mode == mode && it.section == null) it
            else it.copy(mode = mode, section = null, searchQuery = if (it.mode == mode) it.searchQuery else "")
        }
    }

    fun toggleMode() {
        selectMode(if (_uiState.value.mode == HubMode.DISCOVER) HubMode.STORE else HubMode.DISCOVER)
    }

    fun openSection(section: HubSection) {
        _uiState.update {
            it.copy(
                mode = HubMode.DISCOVER,
                section = section,
                savedReturnMode = if (section == HubSection.SAVED) it.mode else HubMode.DISCOVER,
                searchQuery = ""
            )
        }
    }

    /** Closes the open section (or Saved list); returns false when there was nothing to close. */
    fun closeSection(): Boolean {
        val state = _uiState.value
        if (state.section == null) return false
        _uiState.update {
            it.copy(
                mode = if (it.section == HubSection.SAVED) it.savedReturnMode else HubMode.DISCOVER,
                section = null,
                searchQuery = ""
            )
        }
        return true
    }

    fun setSearchQuery(query: String) {
        _uiState.update { it.copy(searchQuery = query) }
    }

    fun clearSearch() = setSearchQuery("")

    fun setSortOrder(sortOrder: HubSortOrder) {
        _uiState.update { it.copy(sortOrder = sortOrder) }
    }

    fun setProductFilter(productFilter: HubProductFilter) {
        _uiState.update { it.copy(productFilter = productFilter) }
    }

    fun setFeaturedOnly(featuredOnly: Boolean) {
        _uiState.update { it.copy(featuredOnly = featuredOnly) }
    }

    fun clearFilters() {
        _uiState.update {
            it.copy(
                productFilter = HubProductFilter.ALL,
                featuredOnly = false
            )
        }
    }

    fun markInitialSkeletonShown() {
        hasShownInitialSkeleton = true
    }

    fun refresh(force: Boolean = true, showIndicator: Boolean = true) {
        if (_uiState.value.isRefreshing) return
        viewModelScope.launch {
            _uiState.update {
                it.copy(
                    isRefreshing = true,
                    showRefreshIndicator = showIndicator,
                    error = null
                )
            }
            runCatching { repository.sync(locale, forceRefresh = force) }
                .onSuccess { result ->
                    _uiState.update {
                        it.copy(
                            isInitialLoading = false,
                            isRefreshing = false,
                            showRefreshIndicator = false,
                            isOffline = false,
                            error = null,
                            fallbackLocale = result.fallbackLocale
                        )
                    }
                }
                .onFailure { error ->
                    _uiState.update {
                        it.copy(
                            isInitialLoading = false,
                            isRefreshing = false,
                            showRefreshIndicator = false,
                            isOffline = it.items.isNotEmpty(),
                            error = error
                        )
                    }
                }
        }
    }

    fun toggleFavorite(item: HubItem) {
        viewModelScope.launch { repository.toggleFavorite(item) }
    }

    private fun observeItems() {
        itemsJob?.cancel()
        itemsJob = viewModelScope.launch {
            repository.observeItems(locale).collectLatest { items ->
                _uiState.update {
                    it.copy(
                        items = items,
                        isInitialLoading = items.isEmpty() && it.isInitialLoading
                    )
                }
            }
        }
    }
}

private fun String.normalizedSearchText(): String = Normalizer.normalize(this, Normalizer.Form.NFKC)
    .lowercase(Locale.ROOT)
    .replace(Regex("[\\p{Punct}\\s]+"), " ")
    .trim()

private fun HubItem.matchesProductFilter(filter: HubProductFilter): Boolean {
    val products = relatedProductIds.mapTo(mutableSetOf()) { it.lowercase(Locale.ROOT) }
    return when (filter) {
        HubProductFilter.ALL -> true
        HubProductFilter.EMUCOREX -> "emucorex" in products
        HubProductFilter.EMUCOREV -> "emucorev" in products
        HubProductFilter.PLAYSTATION_2 -> products.any { it in PS2_PRODUCT_IDS }
        HubProductFilter.OTHER_EMULATORS -> products.none {
            it == "emucorex" || it == "emucorev" || it in PS2_PRODUCT_IDS
        }
    }
}

private fun HubSortOrder.comparator(): Comparator<HubItem> = when (this) {
    HubSortOrder.NEWEST_FIRST -> compareByDescending<HubItem> { it.sortDateKey() }
        .thenByDescending(HubItem::updatedAt)
        .thenBy { it.title.lowercase(Locale.ROOT) }
        .thenBy(HubItem::id)
    HubSortOrder.OLDEST_FIRST -> compareBy<HubItem> { it.sortDateKey() }
        .thenBy(HubItem::updatedAt)
        .thenBy { it.title.lowercase(Locale.ROOT) }
        .thenBy(HubItem::id)
    HubSortOrder.TITLE_ASCENDING -> compareBy<HubItem> { it.title.lowercase(Locale.ROOT) }
        .thenByDescending { it.sortDateKey() }
        .thenBy(HubItem::id)
}

private fun HubItem.sortDateKey(): String = when (kind) {
    HubKind.HISTORY -> eventDate?.takeIf(String::isNotBlank) ?: publishedAt
    else -> publishedAt
}

private val PS2_PRODUCT_IDS = setOf(
    "ps2",
    "playstation-2",
    "pcsx2",
    "aethersx2",
    "nethersx2",
    "armsx2"
)
