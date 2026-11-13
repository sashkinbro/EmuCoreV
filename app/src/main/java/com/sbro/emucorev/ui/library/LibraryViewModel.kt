package com.sbro.emucorev.ui.library

import android.app.Application
import android.content.Context
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.core.PlayTimeRepository
import com.sbro.emucorev.core.InstallStateBus
import com.sbro.emucorev.data.InstalledGameRepository
import com.sbro.emucorev.data.InstalledVitaGame
import com.sbro.emucorev.ui.common.localBitmapLoader
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

data class LibraryUiState(
    val items: List<InstalledVitaGame> = emptyList(),
    val query: String = "",
    val isLoading: Boolean = true,
    val artworkVersion: Long = 0L,
    val recentTitleId: String? = null,
    val sortOption: LibrarySortOption = LibrarySortOption.TITLE_ASC,
    val isSorting: Boolean = false
)

class LibraryViewModel(application: Application) : AndroidViewModel(application) {

    private val repository = InstalledGameRepository()
    private var allItems: List<InstalledVitaGame> = emptyList()
    private val playTimeRepository = PlayTimeRepository(application)
    private var recentTitleIds: List<String> = emptyList()
    private var sortMetadata: Map<String, LibraryGameSortMetadata> = emptyMap()
    private var sizesLoaded = false
    private var refreshJob: Job? = null
    private var sizeJob: Job? = null
    private val uiPreferences = application.getSharedPreferences("emucorev_library_ui", Context.MODE_PRIVATE)

    private val _uiState = MutableStateFlow(LibraryUiState(
        sortOption = LibrarySortOption.fromPreferenceValue(uiPreferences.getString("sort_option", null))
    ))
    val uiState: StateFlow<LibraryUiState> = _uiState.asStateFlow()

    init {
        refresh()
        viewModelScope.launch {
            InstallStateBus.events.collect {
                refresh()
            }
        }
    }

    fun refresh() {
        val context = getApplication<Application>()
        refreshJob?.cancel()
        sizeJob?.cancel()
        refreshJob = viewModelScope.launch {
            _uiState.value = _uiState.value.copy(isLoading = true, isSorting = false)
            val snapshot = withContext(Dispatchers.IO) {
                val games = repository.loadInstalledGames(context)
                val metadata = games.associate { it.titleId to readInstalledGameSortMetadata(it, includeSize = false) }
                val recent = playTimeRepository.loadSessions().sortedByDescending { it.startedAt }.map { it.titleId }.distinct()
                Triple(games, metadata, recent)
            }
            allItems = snapshot.first
            sortMetadata = snapshot.second
            recentTitleIds = snapshot.third
            sizesLoaded = false
            _uiState.value = _uiState.value.copy(isLoading = false)
            publishState(artworkVersion = localBitmapLoader.invalidate())
            if (_uiState.value.sortOption.requiresSize) loadInstalledSizes()
        }
    }

    fun updateSortOption(option: LibrarySortOption) {
        uiPreferences.edit().putString("sort_option", option.name).apply()
        sizeJob?.cancel()
        _uiState.value = _uiState.value.copy(sortOption = option, isSorting = false)
        publishState()
        if (option.requiresSize && !sizesLoaded && !_uiState.value.isLoading) loadInstalledSizes()
    }

    private fun loadInstalledSizes() {
        if (allItems.isEmpty()) return
        val games = allItems
        _uiState.value = _uiState.value.copy(isSorting = true)
        sizeJob = viewModelScope.launch {
            sortMetadata = withContext(Dispatchers.IO) {
                games.associate { game ->
                    game.titleId to readInstalledGameSortMetadata(game, includeSize = true) { coroutineContext.ensureActive() }
                }
            }
            sizesLoaded = true
            _uiState.value = _uiState.value.copy(isSorting = false)
            publishState()
        }
    }

    fun deleteInstalledGame(titleId: String, onComplete: (Boolean) -> Unit) {
        val context = getApplication<Application>()
        viewModelScope.launch {
            val deleted = withContext(Dispatchers.IO) { repository.deleteByTitleId(context, titleId) }
            if (deleted) {
                allItems = allItems.filterNot { it.titleId.equals(titleId, ignoreCase = true) }
                publishState()
                InstallStateBus.notifyCompleted()
            }
            onComplete(deleted)
        }
    }

    fun updateQuery(query: String) {
        _uiState.value = _uiState.value.copy(query = query)
        publishState()
    }

    private fun publishState(artworkVersion: Long = _uiState.value.artworkVersion) {
        val query = _uiState.value.query.trim()
        val filteredItems = allItems.filter {
            query.isBlank() ||
                it.title.contains(query, ignoreCase = true) ||
                it.titleId.contains(query, ignoreCase = true)
        }
        _uiState.value = _uiState.value.copy(
            items = sortInstalledGames(filteredItems, _uiState.value.sortOption, sortMetadata),
            artworkVersion = artworkVersion,
            recentTitleId = recentTitleIds.firstOrNull { id ->
                filteredItems.any { it.titleId.equals(id, ignoreCase = true) }
            }
        )
    }
}
