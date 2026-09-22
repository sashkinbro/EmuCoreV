package com.sbro.emucorev.ui.savestates

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.core.SaveStateBridge
import com.sbro.emucorev.core.SaveStateRepository
import com.sbro.emucorev.core.SaveStateResult
import com.sbro.emucorev.core.SaveStateSlot
import com.sbro.emucorev.core.VitaLaunchBridge
import com.sbro.emucorev.data.InstalledGameRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

data class SaveStatesGame(
    val titleId: String,
    val title: String,
    val iconPath: String?,
    val slots: List<SaveStateSlot>
)

data class SaveStatesUiState(
    val games: List<SaveStatesGame> = emptyList(),
    val isLoading: Boolean = true,
    val busyKey: String? = null,
    val runningTitleId: String = ""
)

class SaveStatesViewModel(application: Application) : AndroidViewModel(application) {

    private val repository = SaveStateRepository(application)
    private val installedGames = InstalledGameRepository()

    private val _uiState = MutableStateFlow(SaveStatesUiState())
    val uiState: StateFlow<SaveStatesUiState> = _uiState.asStateFlow()

    fun refresh(focusTitleId: String? = null) {
        val context = getApplication<Application>()
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(isLoading = true)
            val titleIds = if (!focusTitleId.isNullOrBlank()) {
                listOf(focusTitleId)
            } else {
                repository.gamesWithSlots()
            }
            val runningTitleId = SaveStateBridge.runningTitleId()
            val games = titleIds.mapNotNull { titleId ->
                val game = installedGames.findByTitleId(context, titleId)
                val slots = repository.listSlots(titleId).filter { it.exists }
                if (slots.isEmpty()) {
                    null
                } else {
                    SaveStatesGame(
                        titleId = titleId,
                        title = game?.title?.takeIf { it.isNotBlank() } ?: titleId,
                        iconPath = game?.iconPath,
                        slots = slots
                    )
                }
            }
            _uiState.value = SaveStatesUiState(
                games = games,
                isLoading = false,
                runningTitleId = runningTitleId
            )
        }
    }

    fun load(titleId: String, slot: Int, onComplete: (SaveStateResult) -> Unit) {
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(busyKey = key(titleId, slot))
            val result = repository.load(titleId, slot)
            _uiState.value = _uiState.value.copy(busyKey = null)
            withContext(Dispatchers.Main) { onComplete(result) }
        }
    }

    fun launchWithState(titleId: String, slot: Int, onComplete: (VitaLaunchBridge.LaunchResult) -> Unit) {
        viewModelScope.launch {
            val result = VitaLaunchBridge.launchInstalledTitleWithSaveState(
                getApplication(),
                titleId,
                repository.stateFile(titleId, slot).absolutePath
            )
            withContext(Dispatchers.Main) { onComplete(result) }
        }
    }

    fun delete(titleId: String, slot: Int, onComplete: (SaveStateResult) -> Unit) {
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(busyKey = key(titleId, slot))
            val result = repository.delete(titleId, slot)
            _uiState.value = _uiState.value.copy(busyKey = null)
            withContext(Dispatchers.Main) { onComplete(result) }
        }
    }

    fun isRunning(titleId: String): Boolean =
        titleId.equals(_uiState.value.runningTitleId, ignoreCase = true)

    private fun key(titleId: String, slot: Int): String = "$titleId:$slot"
}
