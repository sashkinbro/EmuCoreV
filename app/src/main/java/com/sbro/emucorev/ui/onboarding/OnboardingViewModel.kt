package com.sbro.emucorev.ui.onboarding

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.core.InstallStateBus
import com.sbro.emucorev.core.VitaStorageLocation
import com.sbro.emucorev.data.AppPreferences
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

data class OnboardingUiState(
    val currentPage: Int = 0,
    val totalPages: Int = 4,
    val storagePath: String = "",
    val storageLocations: List<VitaStorageLocation> = emptyList(),
    val canContinue: Boolean = true
)

class OnboardingViewModel(application: Application) : AndroidViewModel(application) {

    private val preferences = AppPreferences(application)

    private val _uiState = MutableStateFlow(
        OnboardingUiState(
            storagePath = EmulatorStorage.vitaRoot(application).absolutePath,
            storageLocations = EmulatorStorage.availableStorageLocations(application),
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

    fun selectStorageLocation(rootPath: String) {
        val context = getApplication<Application>()
        EmulatorStorage.selectStorageRoot(context, rootPath)
        _uiState.value = _uiState.value.copy(
            storagePath = EmulatorStorage.vitaRoot(context).absolutePath,
            storageLocations = EmulatorStorage.availableStorageLocations(context)
        )
        InstallStateBus.notifyCompleted()
    }
}
