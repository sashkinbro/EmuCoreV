package com.sbro.emucorev.ui.catalog

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.data.VitaCompatibilityRepository
import com.sbro.emucorev.data.VitaCatalogEntry
import com.sbro.emucorev.data.VitaCatalogRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class CatalogUiState(
    val query: String = "",
    val isLoading: Boolean = false,
    val isLoadingMore: Boolean = false,
    val hasCatalog: Boolean = false,
    val hasMore: Boolean = false,
    val results: List<VitaCatalogEntry> = emptyList(),
    val availableGenres: List<String> = emptyList(),
    val availableYears: List<Int> = emptyList(),
    val selectedGenre: String? = null,
    val selectedYear: Int? = null,
    val minRating: Float? = null
)

class CatalogViewModel(application: Application) : AndroidViewModel(application) {
    companion object {
        private const val PAGE_SIZE = 48
    }

    private val repository = VitaCatalogRepository(application)
    private val compatibilityRepository = VitaCompatibilityRepository(application)
    private var refreshJob: Job? = null
    private var loadMoreJob: Job? = null
    private var started = false

    private val _uiState = MutableStateFlow(CatalogUiState())
    val uiState: StateFlow<CatalogUiState> = _uiState.asStateFlow()

    fun onScreenStart() {
        if (started) return
        started = true
        if (_uiState.value.results.isEmpty() && !_uiState.value.hasCatalog) {
            refresh(showFullscreenLoader = true)
        }
    }

    fun onScreenStop() {
        refreshJob?.cancel()
        loadMoreJob?.cancel()
        started = false
    }

    fun updateQuery(query: String) {
        _uiState.value = _uiState.value.copy(query = query)
        scheduleRefresh()
    }

    fun updateGenre(genre: String?) {
        _uiState.value = _uiState.value.copy(selectedGenre = genre)
        refresh(showFullscreenLoader = false)
    }

    fun updateYear(year: Int?) {
        _uiState.value = _uiState.value.copy(selectedYear = year)
        refresh(showFullscreenLoader = false)
    }

    fun updateMinRating(minRating: Float?) {
        _uiState.value = _uiState.value.copy(minRating = minRating)
        refresh(showFullscreenLoader = false)
    }

    fun clearFilters() {
        _uiState.value = _uiState.value.copy(
            selectedGenre = null,
            selectedYear = null,
            minRating = null
        )
        refresh(showFullscreenLoader = false)
    }

    private fun scheduleRefresh() {
        refreshJob?.cancel()
        refreshJob = viewModelScope.launch {
            delay(220)
            refresh(showFullscreenLoader = false)
        }
    }

    fun refresh(showFullscreenLoader: Boolean = false) {
        refreshJob?.cancel()
        val currentState = _uiState.value
        _uiState.value = currentState.copy(
            isLoading = showFullscreenLoader,
            isLoadingMore = false,
            hasMore = false
        )
        refreshJob = viewModelScope.launch(Dispatchers.IO) {
            val snapshot = _uiState.value
            val hasCatalog = repository.hasCatalog()
            val availableGenres = if (hasCatalog && snapshot.availableGenres.isEmpty()) {
                repository.getAvailableGenres()
            } else {
                snapshot.availableGenres
            }
            val availableYears = if (hasCatalog && snapshot.availableYears.isEmpty()) {
                repository.getAvailableYears()
            } else {
                snapshot.availableYears
            }
            val rawResults = if (hasCatalog) {
                repository.search(
                    query = snapshot.query,
                    genre = snapshot.selectedGenre,
                    year = snapshot.selectedYear,
                    minRating = snapshot.minRating,
                    limit = PAGE_SIZE,
                    offset = 0
                )
            } else {
                emptyList()
            }
            val compatibilitySnapshot = compatibilityRepository.getSnapshot()
            val results = rawResults.attachCompatibility(compatibilitySnapshot)
            _uiState.value = _uiState.value.copy(
                isLoading = false,
                hasCatalog = hasCatalog,
                hasMore = hasCatalog && results.size >= PAGE_SIZE,
                results = results,
                availableGenres = availableGenres,
                availableYears = availableYears
            )
        }
    }

    fun loadMoreIfNeeded(lastVisibleIndex: Int) {
        val state = _uiState.value
        if (state.isLoading || state.isLoadingMore || !state.hasCatalog || !state.hasMore) return
        if (lastVisibleIndex < state.results.lastIndex - 8) return

        loadMoreJob?.cancel()
        loadMoreJob = viewModelScope.launch(Dispatchers.IO) {
            val snapshot = _uiState.value
            _uiState.value = snapshot.copy(isLoadingMore = true)
            val compatibilitySnapshot = compatibilityRepository.getSnapshot()
            val nextPage = repository.search(
                query = snapshot.query,
                genre = snapshot.selectedGenre,
                year = snapshot.selectedYear,
                minRating = snapshot.minRating,
                limit = PAGE_SIZE,
                offset = snapshot.results.size
            ).attachCompatibility(compatibilitySnapshot)
            val merged = snapshot.results + nextPage.filterNot { next ->
                snapshot.results.any { it.igdbId == next.igdbId }
            }
            _uiState.value = _uiState.value.copy(
                isLoadingMore = false,
                results = merged,
                hasMore = nextPage.size >= PAGE_SIZE
            )
        }
    }

    private fun List<VitaCatalogEntry>.attachCompatibility(
        snapshot: com.sbro.emucorev.data.VitaCompatibilitySnapshot
    ): List<VitaCatalogEntry> {
        return map { entry ->
            entry.copy(compatibility = snapshot.resolve(entry.serials, gameName = entry.name))
        }
    }
}
