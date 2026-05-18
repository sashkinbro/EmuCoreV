package com.sbro.emucorev.ui.achievements

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.core.InstallStateBus
import com.sbro.emucorev.data.AchievementAssistRepository
import com.sbro.emucorev.data.AchievementAssistResult
import com.sbro.emucorev.data.AppLanguage
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.VitaTrophy
import com.sbro.emucorev.data.TrophyRepository
import com.sbro.emucorev.data.VitaTrophySet
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.util.Locale

data class TrophyAssistUiState(
    val translation: AchievementAssistResult? = null,
    val hint: AchievementAssistResult? = null,
    val translationLoading: Boolean = false,
    val hintLoading: Boolean = false,
    val hasError: Boolean = false
)

data class AchievementsUiState(
    val sets: List<VitaTrophySet> = emptyList(),
    val selectedCommunicationId: String? = null,
    val isLoading: Boolean = true,
    val targetLanguageTag: String = "en",
    val targetLanguageName: String = "English",
    val trophyAssist: Map<String, TrophyAssistUiState> = emptyMap()
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
    private val assistRepository = AchievementAssistRepository(application)
    private val preferences = AppPreferences(application)
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
        val assist = _uiState.value.trophyAssist
        val language = resolveTargetLanguage()
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
                isLoading = false,
                targetLanguageTag = language.tag,
                targetLanguageName = language.name,
                trophyAssist = assist
            )
        }
    }

    fun selectSet(communicationId: String) {
        _uiState.value = _uiState.value.copy(selectedCommunicationId = communicationId)
    }

    fun assistKey(set: VitaTrophySet, trophy: VitaTrophy): String = "${set.communicationId}:${trophy.id}"

    fun requestTranslation(set: VitaTrophySet, trophy: VitaTrophy) {
        val state = _uiState.value
        if (state.targetLanguageTag.startsWith("en", ignoreCase = true)) return
        val key = assistKey(set, trophy)
        val current = state.trophyAssist[key]
        if (current?.translationLoading == true || current?.translation != null) return
        updateAssist(key) {
            it.copy(translationLoading = true, hasError = false)
        }
        viewModelScope.launch(Dispatchers.IO) {
            val result = runCatching {
                assistRepository.translate(
                    languageTag = state.targetLanguageTag,
                    languageName = state.targetLanguageName,
                    gameTitle = set.gameTitle,
                    trophyName = trophy.name,
                    trophyDetail = trophy.detail,
                    trophyGrade = trophy.grade.name
                )
            }
            updateAssist(key) { assist ->
                result.fold(
                    onSuccess = { response ->
                        assist.copy(translation = response, translationLoading = false, hasError = false)
                    },
                    onFailure = {
                        Log.w(TAG, "Translation request failed", it)
                        assist.copy(translationLoading = false, hasError = true)
                    }
                )
            }
        }
    }

    fun requestHint(set: VitaTrophySet, trophy: VitaTrophy) {
        val state = _uiState.value
        val key = assistKey(set, trophy)
        val current = state.trophyAssist[key]
        if (current?.hintLoading == true || current?.hint != null) return
        updateAssist(key) {
            it.copy(hintLoading = true, hasError = false)
        }
        viewModelScope.launch(Dispatchers.IO) {
            val result = runCatching {
                assistRepository.hint(
                    languageTag = state.targetLanguageTag,
                    languageName = state.targetLanguageName,
                    gameTitle = set.gameTitle,
                    trophyName = trophy.name,
                    trophyDetail = trophy.detail,
                    trophyGrade = trophy.grade.name
                )
            }
            updateAssist(key) { assist ->
                result.fold(
                    onSuccess = { response ->
                        assist.copy(hint = response, hintLoading = false, hasError = false)
                    },
                    onFailure = {
                        Log.w(TAG, "Hint request failed", it)
                        assist.copy(hintLoading = false, hasError = true)
                    }
                )
            }
        }
    }

    private fun updateAssist(key: String, transform: (TrophyAssistUiState) -> TrophyAssistUiState) {
        val current = _uiState.value
        val next = current.trophyAssist.toMutableMap()
        next[key] = transform(next[key] ?: TrophyAssistUiState())
        _uiState.value = current.copy(trophyAssist = next)
    }

    private fun resolveTargetLanguage(): AssistLanguage {
        val language = preferences.appLanguage
        val locale = if (language == AppLanguage.SYSTEM) {
            Locale.getDefault()
        } else {
            Locale.forLanguageTag(language.languageTag)
        }
        val tag = locale.toLanguageTag().takeIf { it.isNotBlank() } ?: "en"
        val name = locale.getDisplayLanguage(Locale.ENGLISH).takeIf { it.isNotBlank() } ?: tag
        return AssistLanguage(tag = tag, name = name)
    }

    private data class AssistLanguage(
        val tag: String,
        val name: String
    )

    private companion object {
        const val TAG = "AchievementsAssist"
    }
}
