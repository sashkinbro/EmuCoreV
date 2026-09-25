package com.sbro.emucorev.ui.profile

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.data.ProfileCatalogGame
import com.sbro.emucorev.data.ProfileGameListRepository
import com.sbro.emucorev.data.ProfileGameStatus
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

enum class MyListsLayoutMode {
    GRID,
    LIST
}

data class MyListsUiState(
    val isLoading: Boolean = true,
    val layoutMode: MyListsLayoutMode = MyListsLayoutMode.GRID,
    val gamesByStatus: Map<ProfileGameStatus, List<ProfileCatalogGame>> = emptyMap(),
    val favoriteGames: List<ProfileCatalogGame> = emptyList()
) {
    val totalCount: Int
        get() = (gamesByStatus.values.flatten() + favoriteGames).distinctBy { it.catalog.igdbId }.size

    val visibleStatuses: List<ProfileGameStatus>
        get() = ProfileGameStatus.entries.filter { gamesByStatus[it].orEmpty().isNotEmpty() }
}

class MyListsViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = ProfileGameListRepository(application)

    private val _uiState = MutableStateFlow(MyListsUiState())
    val uiState: StateFlow<MyListsUiState> = _uiState.asStateFlow()

    init {
        refresh()
    }

    fun refresh() {
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(isLoading = true)
            val games = repository.loadCatalogGames()
            _uiState.value = _uiState.value.copy(
                isLoading = false,
                gamesByStatus = games.byStatus(),
                favoriteGames = games.favorites()
            )
        }
    }

    fun setLayoutMode(mode: MyListsLayoutMode) {
        _uiState.value = _uiState.value.copy(layoutMode = mode)
    }

    fun removeGame(igdbId: Long) {
        viewModelScope.launch(Dispatchers.IO) {
            repository.remove(igdbId)
            refreshGames()
        }
    }

    fun setGameStatus(igdbId: Long, status: ProfileGameStatus) {
        viewModelScope.launch(Dispatchers.IO) {
            repository.setStatus(igdbId, status)
            refreshGames()
        }
    }

    fun clearGameStatus(igdbId: Long) {
        viewModelScope.launch(Dispatchers.IO) {
            repository.clearStatus(igdbId)
            refreshGames()
        }
    }

    fun setFavorite(igdbId: Long, favorite: Boolean) {
        viewModelScope.launch(Dispatchers.IO) {
            repository.setFavorite(igdbId, favorite)
            refreshGames()
        }
    }

    private fun refreshGames() {
        val games = repository.loadCatalogGames()
        _uiState.value = _uiState.value.copy(
            gamesByStatus = games.byStatus(),
            favoriteGames = games.favorites()
        )
    }

    private fun List<ProfileCatalogGame>.byStatus(): Map<ProfileGameStatus, List<ProfileCatalogGame>> {
        return mapNotNull { game -> game.profile.status?.let { it to game } }
            .groupBy({ it.first }, { it.second })
    }

    private fun List<ProfileCatalogGame>.favorites(): List<ProfileCatalogGame> {
        return filter { it.profile.isFavorite }
    }
}
