package com.sbro.emucorev.ui.saves

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.data.SaveDataImportResult
import com.sbro.emucorev.data.SaveDataRepository
import com.sbro.emucorev.data.VitaSaveDataEntry
import com.sbro.emucorev.data.VitaSaveDataTarget
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

data class SaveDataUiState(
    val saves: List<VitaSaveDataEntry> = emptyList(),
    val query: String = "",
    val isLoading: Boolean = true,
    val busySaveId: String? = null,
    val focusTarget: VitaSaveDataTarget? = null
)

class SaveDataViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = SaveDataRepository()
    private var allSaves: List<VitaSaveDataEntry> = emptyList()

    private val _uiState = MutableStateFlow(SaveDataUiState())
    val uiState: StateFlow<SaveDataUiState> = _uiState.asStateFlow()

    init {
        refresh()
    }

    fun refresh(focusTitleId: String? = null) {
        val context = getApplication<Application>()
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(isLoading = true)
            val focusTarget = focusTitleId?.let { repository.targetForTitleId(context, it) }
            allSaves = repository.list(context)
            _uiState.value = _uiState.value.copy(focusTarget = focusTarget)
            publishState()
        }
    }

    fun updateQuery(query: String) {
        _uiState.value = _uiState.value.copy(query = query)
        publishState()
    }

    fun delete(saveId: String, onComplete: (Boolean) -> Unit) {
        val context = getApplication<Application>()
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(busySaveId = saveId)
            val deleted = repository.delete(context, saveId)
            if (deleted) {
                allSaves = repository.list(context)
                publishState()
            }
            _uiState.value = _uiState.value.copy(busySaveId = null)
            withContext(Dispatchers.Main) { onComplete(deleted) }
        }
    }

    fun exportSave(saveId: String, destination: Uri, onComplete: (Result<Unit>) -> Unit) {
        val context = getApplication<Application>()
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(busySaveId = saveId)
            val result = repository.exportToZip(context, saveId, destination)
            _uiState.value = _uiState.value.copy(busySaveId = null)
            withContext(Dispatchers.Main) { onComplete(result) }
        }
    }

    fun importSave(source: Uri, targetSaveId: String?, onComplete: (SaveDataImportResult) -> Unit) {
        val context = getApplication<Application>()
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(busySaveId = targetSaveId)
            val result = repository.importFromZip(context, source, targetSaveId)
            allSaves = repository.list(context)
            publishState()
            _uiState.value = _uiState.value.copy(busySaveId = null)
            withContext(Dispatchers.Main) { onComplete(result) }
        }
    }

    private fun publishState() {
        val query = _uiState.value.query.trim()
        val focusTarget = _uiState.value.focusTarget
        val scopedSaves = if (focusTarget != null) {
            allSaves.filter { it.saveId == focusTarget.saveId || it.titleId == focusTarget.titleId }
        } else {
            allSaves
        }
        val filtered = scopedSaves.filter { save ->
            query.isBlank() ||
                save.title.contains(query, ignoreCase = true) ||
                save.saveId.contains(query, ignoreCase = true) ||
                save.titleId?.contains(query, ignoreCase = true) == true
        }
        _uiState.value = _uiState.value.copy(
            saves = filtered,
            isLoading = false
        )
    }
}
