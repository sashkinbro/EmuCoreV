package com.sbro.emucorev.ui.home

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.core.InstallStateBus
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.InstalledVitaGame
import com.sbro.emucorev.data.InstalledGameRepository
import com.sbro.emucorev.data.VitaCatalogRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class HomeUiState(
    val installedCount: Int = 0,
    val catalogCount: Int = 0,
    val storagePath: String = "",
    val packagesFolderLabel: String? = null,
    val featuredGames: List<InstalledVitaGame> = emptyList(),
    val isLoading: Boolean = true
)

class HomeViewModel(application: Application) : AndroidViewModel(application) {

    private val preferences = AppPreferences(application)
    private val installedRepository = InstalledGameRepository()
    private val catalogRepository = VitaCatalogRepository(application)

    private val _uiState = MutableStateFlow(
        HomeUiState(storagePath = EmulatorStorage.vitaRoot(application).absolutePath)
    )
    val uiState: StateFlow<HomeUiState> = _uiState.asStateFlow()

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
            val installedGames = installedRepository.loadInstalledGames(context)
            _uiState.value = _uiState.value.copy(
                installedCount = installedGames.size,
                catalogCount = catalogRepository.getCatalogCount(),
                packagesFolderLabel = preferences.packagesFolderDisplayName(context),
                storagePath = EmulatorStorage.vitaRoot(context).absolutePath,
                featuredGames = installedGames.take(6),
                isLoading = false
            )
        }
    }

    fun onPackagesFolderSelected(uri: Uri) {
        val context = getApplication<Application>()
        preferences.setPackagesFolder(context, uri)
        _uiState.value = _uiState.value.copy(
            packagesFolderLabel = preferences.packagesFolderDisplayName(context)
        )
    }
}
