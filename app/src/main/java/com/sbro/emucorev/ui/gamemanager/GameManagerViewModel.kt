package com.sbro.emucorev.ui.gamemanager

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.core.InstallStateBus
import com.sbro.emucorev.core.GpuDriverManager
import com.sbro.emucorev.core.InstalledGpuDriver
import com.sbro.emucorev.core.VitaCoreConfig
import com.sbro.emucorev.core.FifaCompatibilityPolicy
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.core.VitaGameSettingsRepository
import com.sbro.emucorev.data.InstalledGameRepository
import com.sbro.emucorev.data.InstalledVitaGame
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking

data class GameManagerUiState(
    val games: List<InstalledVitaGame> = emptyList(),
    val selectedTitleId: String? = null,
    val config: VitaCoreConfig = VitaCoreConfig(),
    val defaults: VitaCoreConfig = VitaCoreConfig(),
    val installedGpuDrivers: List<InstalledGpuDriver> = emptyList(),
    val customDriverOverride: String? = null,
    val hasCustomProfile: Boolean = false,
    val isLoading: Boolean = true
) {
    val selectedGame: InstalledVitaGame?
        get() = games.firstOrNull { it.titleId == selectedTitleId }
}

class GameManagerViewModel(application: Application) : AndroidViewModel(application) {
    private val gameRepository = InstalledGameRepository()
    private val globalRepository = VitaCoreConfigRepository(application)
    private val perGameRepository = VitaGameSettingsRepository(application)
    private val gpuDriverManager = GpuDriverManager(application)
    private val profileSaveQueue = Channel<ProfileWriteRequest>(Channel.UNLIMITED)
    private val latestProfileWrites = linkedMapOf<String, ProfileWriteRequest>()
    private lateinit var profileSaveJob: Job

    private val _uiState = MutableStateFlow(GameManagerUiState())
    val uiState: StateFlow<GameManagerUiState> = _uiState.asStateFlow()

    init {
        profileSaveJob = viewModelScope.launch(Dispatchers.IO) {
            for (request in profileSaveQueue) {
                runCatching {
                    when (request) {
                        is ProfileWriteRequest.Save -> perGameRepository.saveProfile(
                            request.titleId,
                            request.config,
                            request.customDriverOverride
                        )
                        is ProfileWriteRequest.Reset -> perGameRepository.reset(request.titleId)
                    }
                }.onFailure { error ->
                    Log.e(TAG, "Could not persist settings for ${request.titleId}", error)
                }
            }
        }
        refresh()
        viewModelScope.launch {
            InstallStateBus.events.collect {
                refresh(_uiState.value.selectedTitleId)
            }
        }
    }

    fun refresh(preferredTitleId: String? = _uiState.value.selectedTitleId) {
        val context = getApplication<Application>()
        viewModelScope.launch(Dispatchers.IO) {
            val games = gameRepository.loadInstalledGames(context)
            val installedGpuDrivers = gpuDriverManager.listInstalledDrivers()
            val selected = preferredTitleId
                ?.takeIf { id -> games.any { it.titleId == id } }
                ?: games.firstOrNull()?.titleId
            val defaults = globalRepository.ensureDefaultsPersisted()
            val profile = selected?.let(perGameRepository::loadProfile)
            _uiState.value = GameManagerUiState(
                games = games,
                selectedTitleId = selected,
                config = profile?.config ?: defaults,
                defaults = defaults,
                installedGpuDrivers = installedGpuDrivers,
                customDriverOverride = profile?.customDriverOverride,
                hasCustomProfile = selected?.let(perGameRepository::hasCustomConfig) == true,
                isLoading = false
            )
        }
    }

    fun selectGame(titleId: String) {
        if (_uiState.value.selectedTitleId == titleId) return
        refresh(titleId)
    }

    fun updateSelected(transform: (VitaCoreConfig) -> VitaCoreConfig) {
        val titleId = _uiState.value.selectedTitleId ?: return
        val updated = FifaCompatibilityPolicy.apply(
            transform(_uiState.value.config), titleId, _uiState.value.selectedGame?.title.orEmpty()
        )
        val driverOverride = _uiState.value.customDriverOverride
        _uiState.value = _uiState.value.copy(
            config = updated,
            hasCustomProfile = true
        )
        enqueueProfileWrite(ProfileWriteRequest.Save(titleId, updated, driverOverride))
    }

    fun selectCustomDriverOverride(driverName: String?) {
        val titleId = _uiState.value.selectedTitleId ?: return
        val defaults = _uiState.value.defaults
        val updated = _uiState.value.config.copy(customDriverName = driverName ?: defaults.customDriverName)
        _uiState.value = _uiState.value.copy(
            config = updated,
            customDriverOverride = driverName,
            hasCustomProfile = true
        )
        enqueueProfileWrite(ProfileWriteRequest.Save(titleId, updated, driverName))
    }

    fun resetSelectedToGlobal() {
        val titleId = _uiState.value.selectedTitleId ?: return
        val defaults = FifaCompatibilityPolicy.apply(
            _uiState.value.defaults, titleId, _uiState.value.selectedGame?.title.orEmpty()
        )
        _uiState.value = _uiState.value.copy(
            config = defaults,
            customDriverOverride = null,
            hasCustomProfile = false
        )
        enqueueProfileWrite(ProfileWriteRequest.Reset(titleId))
    }

    override fun onCleared() {
        profileSaveQueue.cancel()
        runBlocking { profileSaveJob.cancelAndJoin() }
        latestProfileWrites.values.forEach { request ->
            runCatching {
                when (request) {
                    is ProfileWriteRequest.Save -> perGameRepository.saveProfile(
                        request.titleId,
                        request.config,
                        request.customDriverOverride
                    )
                    is ProfileWriteRequest.Reset -> perGameRepository.reset(request.titleId)
                }
            }.onFailure { error ->
                Log.e(TAG, "Could not flush settings for ${request.titleId}", error)
            }
        }
    }

    private fun enqueueProfileWrite(request: ProfileWriteRequest) {
        latestProfileWrites[request.titleId] = request
        profileSaveQueue.trySend(request)
    }

    private sealed interface ProfileWriteRequest {
        val titleId: String

        data class Save(
            override val titleId: String,
            val config: VitaCoreConfig,
            val customDriverOverride: String?
        ) : ProfileWriteRequest

        data class Reset(override val titleId: String) : ProfileWriteRequest
    }

    private companion object {
        const val TAG = "GameManagerViewModel"
    }
}
