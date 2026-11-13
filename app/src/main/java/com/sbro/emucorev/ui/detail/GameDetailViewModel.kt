package com.sbro.emucorev.ui.detail

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.core.InstallStateBus
import com.sbro.emucorev.data.InstalledGameRepository
import com.sbro.emucorev.data.InstalledVitaGame
import com.sbro.emucorev.data.EcvCompatibilityRepository
import com.sbro.emucorev.data.EcvCompatibilityDataset
import com.sbro.emucorev.data.EcvCompatibilityUiRun
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
    val compatibility: List<EcvCompatibilityUiRun> = emptyList(),
    val isCatalogAvailable: Boolean = false
)

class GameDetailViewModel(application: Application) : AndroidViewModel(application) {

    private val installedRepository = InstalledGameRepository()
    private val catalogRepository = VitaCatalogRepository(application)
    private val compatibilityRepository = EcvCompatibilityRepository(application)
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
            val compatibilitySnapshot = compatibilityRepository.load()
            // An installed title's unknown result must not inherit another region/catalog serial.
            val compatibility = detailCompatibilityRows(compatibilitySnapshot, titleId, rawCatalogEntry?.serials.orEmpty())
            val catalogEntry = rawCatalogEntry?.copy(
                compatibility = compatibilitySnapshot.rowsFor(rawCatalogEntry.serials)
            )
            _uiState.value = GameDetailUiState(
                isLoading = false,
                game = game,
                catalogEntry = catalogEntry,
                compatibility = compatibility,
                isCatalogAvailable = hasCatalog
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

/** Title ID presence is authoritative, even if absent from the reviewed dataset. */
internal fun detailCompatibilityRows(
    dataset: EcvCompatibilityDataset,
    installedTitleId: String?,
    catalogSerials: List<String>
): List<EcvCompatibilityUiRun> = dataset.rowsFor(
    if (installedTitleId != null) listOf(installedTitleId) else catalogSerials
)
