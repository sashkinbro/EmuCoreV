package com.sbro.emucorev.ui.gamemanager

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.core.InstallStateBus
import com.sbro.emucorev.core.GpuDriverManager
import com.sbro.emucorev.core.InstalledGpuDriver
import com.sbro.emucorev.core.VitaCoreConfig
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.core.VitaGameSettingsRepository
import com.sbro.emucorev.data.InstalledGameRepository
import com.sbro.emucorev.data.InstalledVitaGame
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class GameManagerUiState(
    val games: List<InstalledVitaGame> = emptyList(),
    val selectedTitleId: String? = null,
    val config: VitaCoreConfig = VitaCoreConfig(),
    val defaults: VitaCoreConfig = VitaCoreConfig(),
    val installedGpuDrivers: List<InstalledGpuDriver> = emptyList(),
    val hasCustomProfile: Boolean = false,
    val isLoading: Boolean = true
) {
    val selectedGame: InstalledVitaGame?
        get() = games.firstOrNull { it.titleId == selectedTitleId }
}

class GameManagerViewModel(application: Application) : AndroidViewModel(application) {
    private val gameRepository = InstalledGameRepository()
    private val globalRepository = VitaCoreConfigRepository(application)
    private val perGameRepository = VitaGameSettingsRepository(application)
    private val gpuDriverManager = GpuDriverManager(application)

    private val _uiState = MutableStateFlow(GameManagerUiState())
    val uiState: StateFlow<GameManagerUiState> = _uiState.asStateFlow()

    init {
        refresh()
        viewModelScope.launch {
            InstallStateBus.events.collect {
                refresh(_uiState.value.selectedTitleId)
            }
        }
    }

    fun refresh(preferredTitleId: String? = _uiState.value.selectedTitleId) {
        val context = getApplication<Application>()
        viewModelScope.launch(Dispatchers.IO) {
            val games = gameRepository.loadInstalledGames(context)
            val installedGpuDrivers = gpuDriverManager.listInstalledDrivers()
            val selected = preferredTitleId
                ?.takeIf { id -> games.any { it.titleId == id } }
                ?: games.firstOrNull()?.titleId
            val defaults = globalRepository.ensureDefaultsPersisted()
            _uiState.value = GameManagerUiState(
                games = games,
                selectedTitleId = selected,
                config = selected?.let(perGameRepository::loadEffective) ?: defaults,
                defaults = defaults,
                installedGpuDrivers = installedGpuDrivers,
                hasCustomProfile = selected?.let(perGameRepository::hasCustomConfig) == true,
                isLoading = false
            )
        }
    }

    fun selectGame(titleId: String) {
        if (_uiState.value.selectedTitleId == titleId) return
        refresh(titleId)
    }

    fun updateSelected(transform: (VitaCoreConfig) -> VitaCoreConfig) {
        val titleId = _uiState.value.selectedTitleId ?: return
        viewModelScope.launch(Dispatchers.IO) {
            val updated = perGameRepository.update(titleId, transform)
            _uiState.value = _uiState.value.copy(
                config = updated,
                hasCustomProfile = true
            )
        }
    }

    fun resetSelectedToGlobal() {
        val titleId = _uiState.value.selectedTitleId ?: return
        viewModelScope.launch(Dispatchers.IO) {
            perGameRepository.reset(titleId)
            val defaults = globalRepository.ensureDefaultsPersisted()
            _uiState.value = _uiState.value.copy(
                config = defaults,
                defaults = defaults,
                hasCustomProfile = false
            )
        }
    }
}
