package com.sbro.emucorev.ui.onboarding

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.data.AppPreferences
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

data class OnboardingUiState(
    val currentPage: Int = 0,
    val totalPages: Int = 4,
    val storagePath: String = "",
    val canContinue: Boolean = true
)

class OnboardingViewModel(application: Application) : AndroidViewModel(application) {

    private val preferences = AppPreferences(application)

    private val _uiState = MutableStateFlow(
        OnboardingUiState(
            storagePath = EmulatorStorage.vitaRoot(application).absolutePath,
            canContinue = true
        )
    )
    val uiState: StateFlow<OnboardingUiState> = _uiState.asStateFlow()

    fun goNext() {
        _uiState.value = _uiState.value.copy(
            currentPage = (_uiState.value.currentPage + 1).coerceAtMost(_uiState.value.totalPages - 1)
        )
    }

    fun goBack() {
        _uiState.value = _uiState.value.copy(
            currentPage = (_uiState.value.currentPage - 1).coerceAtLeast(0)
        )
    }

    fun setCurrentPage(page: Int) {
        _uiState.value = _uiState.value.copy(
            currentPage = page.coerceIn(0, _uiState.value.totalPages - 1)
        )
    }

    fun completeOnboarding() {
        preferences.onboardingCompleted = true
    }
}
