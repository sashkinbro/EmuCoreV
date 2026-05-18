package com.sbro.emucorev.ui.achievements

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.core.InstallStateBus
import com.sbro.emucorev.data.TrophyRepository
import com.sbro.emucorev.data.VitaTrophySet
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class AchievementsUiState(
    val sets: List<VitaTrophySet> = emptyList(),
    val selectedCommunicationId: String? = null,
    val isLoading: Boolean = true
) {
    val selectedSet: VitaTrophySet?
        get() = selectedCommunicationId
            ?.let { id -> sets.firstOrNull { it.communicationId.equals(id, ignoreCase = true) } }
            ?: sets.firstOrNull()

    val totalTrophies: Int
        get() = sets.sumOf { it.trophyCount }

    val unlockedTrophies: Int
        get() = sets.sumOf { it.unlockedCount }
}

class AchievementsViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = TrophyRepository()
    private var requestedTitleId: String? = null

    private val _uiState = MutableStateFlow(AchievementsUiState())
    val uiState: StateFlow<AchievementsUiState> = _uiState.asStateFlow()

    init {
        refresh()
        viewModelScope.launch {
            InstallStateBus.events.collect {
                refresh()
            }
        }
    }

    fun refresh(focusTitleId: String? = requestedTitleId) {
        val context = getApplication<Application>()
        val selected = _uiState.value.selectedCommunicationId
        val activeTitleId = focusTitleId?.takeIf(String::isNotBlank) ?: requestedTitleId
        requestedTitleId = activeTitleId
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(isLoading = true)
            val sets = repository.list(context)
            val titleIdToSelect = requestedTitleId
            _uiState.value = AchievementsUiState(
                sets = sets,
                selectedCommunicationId = titleIdToSelect
                    ?.let { titleId ->
                        sets.firstOrNull { it.titleId.equals(titleId, ignoreCase = true) }?.communicationId
                    }
                    ?: selected?.takeIf { id ->
                        sets.any { it.communicationId.equals(id, ignoreCase = true) }
                    }
                    ?: sets.firstOrNull()?.communicationId,
                isLoading = false
            )
        }
    }

    fun selectSet(communicationId: String) {
        _uiState.value = _uiState.value.copy(selectedCommunicationId = communicationId)
    }
}
