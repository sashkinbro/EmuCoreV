package com.sbro.emucorev.ui.detail

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.core.InstallStateBus
import com.sbro.emucorev.data.InstalledGameRepository
import com.sbro.emucorev.data.InstalledVitaGame
import com.sbro.emucorev.data.VitaCompatibilityRepository
import com.sbro.emucorev.data.VitaCompatibilitySummary
import com.sbro.emucorev.data.VitaCatalogDetails
import com.sbro.emucorev.data.VitaCatalogRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

data class GameDetailUiState(
    val isLoading: Boolean = true,
    val game: InstalledVitaGame? = null,
    val catalogEntry: VitaCatalogDetails? = null,
    val compatibility: VitaCompatibilitySummary? = null,
    val isCatalogAvailable: Boolean = false,
    val isCompatibilityAvailable: Boolean = false
)

class GameDetailViewModel(application: Application) : AndroidViewModel(application) {

    private val installedRepository = InstalledGameRepository()
    private val catalogRepository = VitaCatalogRepository(application)
    private val compatibilityRepository = VitaCompatibilityRepository(application)
    private var loadedKey: String? = null

    private val _uiState = MutableStateFlow(GameDetailUiState())
    val uiState: StateFlow<GameDetailUiState> = _uiState.asStateFlow()

    fun load(titleId: String?, igdbId: Long?) {
        if (titleId.isNullOrBlank() && igdbId == null) {
            _uiState.value = GameDetailUiState(isLoading = false)
            return
        }
        val loadKey = "title=${titleId.orEmpty()}|igdb=${igdbId ?: -1L}"
        if (loadedKey == loadKey && (_uiState.value.isLoading || _uiState.value.game != null || _uiState.value.catalogEntry != null)) {
            return
        }
        loadedKey = loadKey
        val context = getApplication<Application>()
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = GameDetailUiState(isLoading = true)
            val hasCatalog = catalogRepository.hasCatalog()
            val game = titleId?.let { installedRepository.findByTitleId(context, it) }
            val rawCatalogEntry = when {
                hasCatalog && igdbId != null -> catalogRepository.getDetails(igdbId)
                hasCatalog && game != null -> game.title.let(catalogRepository::findBestMatchDetails)
                else -> null
            }
            val compatibilitySnapshot = compatibilityRepository.getSnapshot()
            val compatibility = game?.titleId?.let(compatibilitySnapshot::resolve)
                ?: compatibilitySnapshot.resolve(rawCatalogEntry?.serials.orEmpty(), gameName = rawCatalogEntry?.name)
            val catalogEntry = rawCatalogEntry?.copy(
                compatibility = compatibilitySnapshot.resolve(rawCatalogEntry.serials, gameName = rawCatalogEntry.name)
            )
            _uiState.value = GameDetailUiState(
                isLoading = false,
                game = game,
                catalogEntry = catalogEntry,
                compatibility = compatibility,
                isCatalogAvailable = hasCatalog,
                isCompatibilityAvailable = compatibilitySnapshot.records.isNotEmpty()
            )
        }
    }

    fun deleteInstalledGame(titleId: String, onComplete: (Boolean) -> Unit) {
        val context = getApplication<Application>()
        viewModelScope.launch(Dispatchers.IO) {
            val deleted = installedRepository.deleteByTitleId(context, titleId)
            if (deleted) {
                loadedKey = null
                InstallStateBus.notifyCompleted()
            }
            withContext(Dispatchers.Main) {
                onComplete(deleted)
            }
        }
    }
}
