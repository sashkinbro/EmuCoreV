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
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
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
    private val actionLock = Any()

    private val _uiState = MutableStateFlow(SaveStatesUiState())
    val uiState: StateFlow<SaveStatesUiState> = _uiState.asStateFlow()

    fun refresh(focusTitleId: String? = null) {
        val context = getApplication<Application>()
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.update { it.copy(isLoading = true) }
            try {
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
                _uiState.update { it.copy(games = games, runningTitleId = runningTitleId) }
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: Exception) {
                // Keep the last usable list; the caller can refresh again after a transient IO failure.
            } finally {
                _uiState.update { it.copy(isLoading = false) }
            }
        }
    }

    fun load(titleId: String, slot: Int, onComplete: (SaveStateResult) -> Unit) {
        if (!beginAction(titleId, slot)) return
        viewModelScope.launch(Dispatchers.IO) {
            val result = try {
                repository.load(titleId, slot)
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (error: Exception) {
                ioError(titleId, error)
            } finally {
                endAction()
            }
            withContext(Dispatchers.Main) { onComplete(result) }
        }
    }

    fun launchWithState(titleId: String, slot: Int, onComplete: (VitaLaunchBridge.LaunchResult) -> Unit) {
        if (!beginAction(titleId, slot)) return
        viewModelScope.launch {
            val result = try {
                VitaLaunchBridge.launchInstalledTitleWithSaveState(
                    getApplication(),
                    titleId,
                    repository.stateFile(titleId, slot).absolutePath
                )
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: Exception) {
                VitaLaunchBridge.LaunchResult.Failure
            } finally {
                endAction()
            }
            withContext(Dispatchers.Main) { onComplete(result) }
        }
    }

    fun delete(titleId: String, slot: Int, onComplete: (SaveStateResult) -> Unit) {
        if (!beginAction(titleId, slot)) return
        viewModelScope.launch(Dispatchers.IO) {
            val result = try {
                repository.delete(titleId, slot)
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (error: Exception) {
                ioError(titleId, error)
            } finally {
                endAction()
            }
            withContext(Dispatchers.Main) { onComplete(result) }
        }
    }

    fun isRunning(titleId: String): Boolean =
        titleId.equals(_uiState.value.runningTitleId, ignoreCase = true)

    private fun key(titleId: String, slot: Int): String = "$titleId:$slot"

    private fun beginAction(titleId: String, slot: Int): Boolean = synchronized(actionLock) {
        if (_uiState.value.busyKey != null) {
            false
        } else {
            _uiState.update { it.copy(busyKey = key(titleId, slot)) }
            true
        }
    }

    private fun endAction() = synchronized(actionLock) {
        _uiState.update { it.copy(busyKey = null) }
    }

    private fun ioError(titleId: String, error: Exception) = SaveStateResult(
        status = SaveStateResult.STATUS_IO_ERROR,
        error = error.message ?: "save state operation failed",
        bytes = 0L,
        titleId = titleId,
        appVersion = "",
        engineVersion = 0,
        sessionMatch = true,
        timestamp = 0L
    )
}
