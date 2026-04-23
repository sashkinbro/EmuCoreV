package com.sbro.emucorev.ui.library

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.core.InstallStateBus
import com.sbro.emucorev.data.InstalledGameRepository
import com.sbro.emucorev.data.InstalledVitaGame
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

data class LibraryUiState(
    val items: List<InstalledVitaGame> = emptyList(),
    val query: String = "",
    val isLoading: Boolean = true
)

class LibraryViewModel(application: Application) : AndroidViewModel(application) {

    private val repository = InstalledGameRepository()
    private var allItems: List<InstalledVitaGame> = emptyList()

    private val _uiState = MutableStateFlow(LibraryUiState())
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
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(isLoading = true)
            allItems = repository.loadInstalledGames(context)
            publishState()
        }
    }

    fun deleteInstalledGame(titleId: String, onComplete: (Boolean) -> Unit) {
        val context = getApplication<Application>()
        viewModelScope.launch(Dispatchers.IO) {
            val deleted = repository.deleteByTitleId(context, titleId)
            if (deleted) {
                allItems = repository.loadInstalledGames(context)
                publishState()
                InstallStateBus.notifyCompleted()
            }
            withContext(Dispatchers.Main) {
                onComplete(deleted)
            }
        }
    }

    fun updateQuery(query: String) {
        _uiState.value = _uiState.value.copy(query = query)
        publishState()
    }

    private fun publishState() {
        val query = _uiState.value.query.trim()
        val filteredItems = allItems.filter {
            query.isBlank() ||
                it.title.contains(query, ignoreCase = true) ||
                it.titleId.contains(query, ignoreCase = true)
        }
        _uiState.value = _uiState.value.copy(
            items = filteredItems,
            isLoading = false
        )
    }
}
