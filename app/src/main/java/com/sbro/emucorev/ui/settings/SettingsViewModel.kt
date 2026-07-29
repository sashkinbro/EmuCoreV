package com.sbro.emucorev.ui.settings

import android.app.Application
import android.net.Uri
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.jakewharton.processphoenix.ProcessPhoenix
import com.sbro.emucorev.core.AppUpdateRelease
import com.sbro.emucorev.core.AppUpdateRepository
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.core.GpuDriverCatalogRepository
import com.sbro.emucorev.core.GpuDriverManager
import com.sbro.emucorev.core.InstallStateBus
import com.sbro.emucorev.core.InstalledGpuDriver
import com.sbro.emucorev.core.NativeLibraryLoader
import com.sbro.emucorev.core.RemoteGpuDriver
import com.sbro.emucorev.core.SettingsBackupRepository
import com.sbro.emucorev.core.StorageMigrationProgress
import com.sbro.emucorev.core.VitaCoreConfig
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.core.VitaStorageLocation
import com.sbro.emucorev.core.VibrationTestController
import com.sbro.emucorev.data.AppLanguage
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.AppFont
import com.sbro.emucorev.data.CustomizationFileStore
import com.sbro.emucorev.data.CustomizationPreferences
import com.sbro.emucorev.data.CustomizationSettings
import com.sbro.emucorev.data.DrawerVisualStyle
import com.sbro.emucorev.data.GameMenuLayoutStyle
import com.sbro.emucorev.data.TouchControlPressEffect
import com.sbro.emucorev.data.TouchControlVisualStyle
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.withContext
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class SettingsUiState(
    val storagePath: String = "",
    val storageLocations: List<VitaStorageLocation> = emptyList(),
    val coreConfig: VitaCoreConfig = VitaCoreConfig(),
    val installedGpuDrivers: List<InstalledGpuDriver> = emptyList(),
    val remoteGpuDrivers: List<RemoteGpuDriver> = emptyList(),
    val gpuDriverCatalogLoading: Boolean = false,
    val gpuDriverCatalogError: String? = null,
    val gpuDriverDownloads: Map<String, Float> = emptyMap(),
    val storageChangeInProgress: Boolean = false,
    val storageMigration: StorageMigrationUiState = StorageMigrationUiState(),
    val appLanguage: AppLanguage = AppLanguage.SYSTEM,
    val customization: CustomizationSettings = CustomizationSettings(),
    val appUpdate: AppUpdateUiState = AppUpdateUiState()
)

data class StorageMigrationUiState(
    val visible: Boolean = false,
    val copiedFiles: Int = 0,
    val skippedFiles: Int = 0,
    val totalFiles: Int = 0,
    val currentPath: String? = null,
    val errorMessage: String? = null
) {
    val progress: Float
        get() = if (totalFiles > 0) {
            (copiedFiles + skippedFiles).toFloat() / totalFiles.toFloat()
        } else {
            0f
        }
}

data class AppUpdateUiState(
    val releaseHistory: List<AppUpdateRelease> = emptyList(),
    val historyLoading: Boolean = false,
    val historyErrorMessage: String? = null
)

class SettingsViewModel(application: Application) : AndroidViewModel(application) {

    private val preferences = AppPreferences(application)
    private val customizationPreferences = CustomizationPreferences(application)
    private val customizationFileStore = CustomizationFileStore(application)
    private val coreConfigRepository = VitaCoreConfigRepository(application)
    private val gpuDriverManager = GpuDriverManager(application)
    private val gpuDriverCatalogRepository = GpuDriverCatalogRepository(application)
    private val appUpdateRepository = AppUpdateRepository(application)
    private val settingsBackupRepository = SettingsBackupRepository(
        application,
        preferences,
        coreConfigRepository,
        customizationPreferences
    )
    private val initialCoreConfig = coreConfigRepository.ensureDefaultsPersisted()
    private val coreSettingsSaveQueue = Channel<VitaCoreConfig>(Channel.CONFLATED)

    private val _uiState = MutableStateFlow(
        SettingsUiState(
            storagePath = EmulatorStorage.vitaRoot(application).absolutePath,
            storageLocations = EmulatorStorage.availableStorageLocations(application),
            coreConfig = initialCoreConfig,
            installedGpuDrivers = gpuDriverManager.listInstalledDrivers(),
            appLanguage = preferences.appLanguage,
            customization = customizationPreferences.current
        )
    )
    val uiState: StateFlow<SettingsUiState> = _uiState.asStateFlow()

    init {
        viewModelScope.launch {
            customizationPreferences.settings.collect { customization ->
                _uiState.value = _uiState.value.copy(customization = customization)
            }
        }
        viewModelScope.launch(Dispatchers.IO) {
            for (config in coreSettingsSaveQueue) {
                runCatching { coreConfigRepository.save(config) }
                    .onFailure { error -> Log.e(TAG, "Could not persist core settings", error) }
            }
        }
    }

    private companion object {
        const val TAG = "SettingsViewModel"
    }

    fun selectStorageLocation(rootPath: String) {
        val context = getApplication<Application>()
        if (EmulatorStorage.storageRoot(context).absolutePath == rootPath) return
        changeStorageLocation { context, onProgress ->
            EmulatorStorage.selectStorageRoot(
                context = context,
                rootPath = rootPath,
                migrateExistingData = true,
                onMigrationProgress = onProgress
            )
        }
    }

    private fun changeStorageLocation(
        selectRoot: (Application, (StorageMigrationProgress) -> Unit) -> Unit
    ) {
        if (_uiState.value.storageChangeInProgress) return
        val context = getApplication<Application>()
        val restartRequired = NativeLibraryLoader.isNativeSessionInitialized()
        _uiState.value = _uiState.value.copy(
            storageChangeInProgress = true,
            storageMigration = StorageMigrationUiState(visible = true)
        )
        viewModelScope.launch(Dispatchers.IO) {
            runCatching {
                selectRoot(context, ::updateStorageMigrationProgress)
                val config = coreConfigRepository.ensureDefaultsPersisted()
                _uiState.value = _uiState.value.copy(
                    storagePath = EmulatorStorage.vitaRoot(context).absolutePath,
                    storageLocations = EmulatorStorage.availableStorageLocations(context),
                    coreConfig = config,
                    storageChangeInProgress = false,
                    storageMigration = StorageMigrationUiState()
                )
                InstallStateBus.notifyCompleted()
            }.onFailure {
                _uiState.value = _uiState.value.copy(
                    storageChangeInProgress = false,
                    storageMigration = _uiState.value.storageMigration.copy(
                        visible = true,
                        errorMessage = it.message ?: "Storage migration failed"
                    )
                )
            }.onSuccess {
                if (restartRequired) {
                    ProcessPhoenix.triggerRebirth(context.applicationContext)
                }
            }
        }
    }

    fun dismissStorageMigrationDialog() {
        if (_uiState.value.storageChangeInProgress) return
        _uiState.value = _uiState.value.copy(storageMigration = StorageMigrationUiState())
    }

    private fun updateStorageMigrationProgress(progress: StorageMigrationProgress) {
        _uiState.value = _uiState.value.copy(
            storageMigration = StorageMigrationUiState(
                visible = true,
                copiedFiles = progress.copiedFiles,
                skippedFiles = progress.skippedFiles,
                totalFiles = progress.totalFiles,
                currentPath = progress.currentPath
            )
        )
    }

    fun exportSettingsBackup(uri: Uri, onComplete: (Result<Unit>) -> Unit) {
        viewModelScope.launch(Dispatchers.IO) {
            val result = runCatching {
                settingsBackupRepository.exportTo(uri)
            }
            withContext(Dispatchers.Main) {
                onComplete(result)
            }
        }
    }

    fun restoreSettingsBackup(uri: Uri, onComplete: (Result<Unit>) -> Unit) {
        viewModelScope.launch(Dispatchers.IO) {
            val result = runCatching {
                val restoredConfig = settingsBackupRepository.restoreFrom(uri)
                val context = getApplication<Application>()
                _uiState.value = _uiState.value.copy(
                    storagePath = EmulatorStorage.vitaRoot(context).absolutePath,
                    storageLocations = EmulatorStorage.availableStorageLocations(context),
                    coreConfig = restoredConfig,
                    installedGpuDrivers = gpuDriverManager.listInstalledDrivers(),
                    appLanguage = preferences.appLanguage
                )
            }
            withContext(Dispatchers.Main) {
                onComplete(result)
            }
        }
    }

    fun refreshCoreSettings() {
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(
                coreConfig = coreConfigRepository.ensureDefaultsPersisted(),
                storagePath = EmulatorStorage.vitaRoot(getApplication()).absolutePath,
                storageLocations = EmulatorStorage.availableStorageLocations(getApplication()),
                installedGpuDrivers = gpuDriverManager.listInstalledDrivers(),
                appLanguage = preferences.appLanguage
            )
        }
    }

    fun resetCoreSettingsToDefaults() {
        viewModelScope.launch(Dispatchers.IO) {
            val defaults = coreConfigRepository.resetToDefaults()
            _uiState.value = _uiState.value.copy(coreConfig = defaults)
        }
    }

    fun updateAppLanguage(language: AppLanguage) {
        if (preferences.appLanguage == language) return
        preferences.appLanguage = language
        _uiState.value = _uiState.value.copy(appLanguage = language)
        preferences.applyAppLanguage()
    }

    fun updateCoverSizePercent(value: Int) {
        customizationPreferences.setCoverSizePercent(value)
    }

    fun updateTextSizePercent(value: Int) {
        customizationPreferences.setTextSizePercent(value)
    }

    fun updateAppFont(font: AppFont) {
        if (font == AppFont.CUSTOM && customizationPreferences.current.customFontPath == null) return
        customizationPreferences.setAppFont(font)
    }

    fun updateTouchControlVisualStyle(style: TouchControlVisualStyle) {
        customizationPreferences.setTouchControlVisualStyle(style)
    }

    fun updateTouchControlPressEffect(effect: TouchControlPressEffect) {
        customizationPreferences.setTouchControlPressEffect(effect)
    }

    fun updateGameMenuLayoutStyle(style: GameMenuLayoutStyle) {
        customizationPreferences.setGameMenuLayoutStyle(style)
    }

    fun updateDrawerVisualStyle(style: DrawerVisualStyle) {
        customizationPreferences.setDrawerVisualStyle(style)
    }

    fun importCustomizationBackground(uri: Uri, onComplete: (Result<Unit>) -> Unit) {
        viewModelScope.launch(Dispatchers.IO) {
            val result = runCatching {
                val imported = customizationFileStore.importBackground(uri)
                customizationPreferences.setBackground(imported.path, imported.mimeType)
            }
            withContext(Dispatchers.Main) { onComplete(result) }
        }
    }

    fun importCustomFont(uri: Uri, onComplete: (Result<Unit>) -> Unit) {
        viewModelScope.launch(Dispatchers.IO) {
            val result = runCatching {
                val path = customizationFileStore.importFont(uri)
                customizationPreferences.setAppFont(AppFont.CUSTOM, path)
            }
            withContext(Dispatchers.Main) { onComplete(result) }
        }
    }

    fun resetCustomization() {
        customizationFileStore.clear()
        customizationPreferences.reset()
    }

    fun updateCoreSettings(transform: (VitaCoreConfig) -> VitaCoreConfig) {
        val updated = transform(_uiState.value.coreConfig)
        _uiState.value = _uiState.value.copy(coreConfig = updated)
        coreSettingsSaveQueue.trySend(updated)
    }

    override fun onCleared() {
        customizationPreferences.close()
        super.onCleared()
    }

    fun testVibration(): Boolean {
        return VibrationTestController.playTestPulse(getApplication(), _uiState.value.coreConfig)
    }

    fun installGpuDriver(uri: Uri, applyGlobally: Boolean = true, onComplete: (Result<String>) -> Unit) {
        viewModelScope.launch(Dispatchers.IO) {
            val result = runCatching {
                val driverName = gpuDriverManager.installFromArchive(uri)
                val installedDrivers = gpuDriverManager.listInstalledDrivers()
                if (applyGlobally) {
                    val updated = _uiState.value.coreConfig.copy(customDriverName = driverName)
                    coreConfigRepository.save(updated)
                    _uiState.value = _uiState.value.copy(
                        coreConfig = updated,
                        installedGpuDrivers = installedDrivers
                    )
                } else {
                    _uiState.value = _uiState.value.copy(installedGpuDrivers = installedDrivers)
                }
                driverName
            }
            withContext(Dispatchers.Main) {
                onComplete(result)
            }
        }
    }

    fun refreshGpuDriverCatalog() {
        if (_uiState.value.gpuDriverCatalogLoading) return
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(
                gpuDriverCatalogLoading = true,
                gpuDriverCatalogError = null
            )
            runCatching {
                gpuDriverCatalogRepository.loadCatalog()
            }.onSuccess { drivers ->
                _uiState.value = _uiState.value.copy(
                    remoteGpuDrivers = drivers,
                    gpuDriverCatalogLoading = false,
                    gpuDriverCatalogError = null
                )
            }.onFailure { error ->
                _uiState.value = _uiState.value.copy(
                    gpuDriverCatalogLoading = false,
                    gpuDriverCatalogError = error.message ?: "Could not load GPU driver catalog"
                )
            }
        }
    }

    fun installRemoteGpuDriver(driver: RemoteGpuDriver, applyGlobally: Boolean = true, onComplete: (Result<String>) -> Unit) {
        if (_uiState.value.gpuDriverDownloads.containsKey(driver.id)) return
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(
                gpuDriverDownloads = _uiState.value.gpuDriverDownloads + (driver.id to 0f)
            )
            val result = runCatching {
                val archive = gpuDriverCatalogRepository.downloadDriver(driver) { progress ->
                    _uiState.value = _uiState.value.copy(
                        gpuDriverDownloads = _uiState.value.gpuDriverDownloads + (driver.id to progress)
                    )
                }
                val driverName = gpuDriverManager.installFromArchive(archive)
                val installedDrivers = gpuDriverManager.listInstalledDrivers()
                if (applyGlobally) {
                    val updated = _uiState.value.coreConfig.copy(customDriverName = driverName)
                    coreConfigRepository.save(updated)
                    _uiState.value = _uiState.value.copy(
                        coreConfig = updated,
                        installedGpuDrivers = installedDrivers
                    )
                } else {
                    _uiState.value = _uiState.value.copy(installedGpuDrivers = installedDrivers)
                }
                driverName
            }
            _uiState.value = _uiState.value.copy(
                gpuDriverDownloads = _uiState.value.gpuDriverDownloads - driver.id
            )
            withContext(Dispatchers.Main) {
                onComplete(result)
            }
        }
    }

    fun useSystemGpuDriver() {
        updateCoreSettings { it.copy(customDriverName = "") }
    }

    fun selectGpuDriver(driverName: String) {
        updateCoreSettings { it.copy(customDriverName = driverName) }
    }

    fun removeGpuDriver(driverName: String) {
        viewModelScope.launch(Dispatchers.IO) {
            gpuDriverManager.remove(driverName)
            val updated = if (_uiState.value.coreConfig.customDriverName == driverName) {
                _uiState.value.coreConfig.copy(customDriverName = "")
            } else {
                _uiState.value.coreConfig
            }
            coreConfigRepository.save(updated)
            _uiState.value = _uiState.value.copy(
                coreConfig = updated,
                installedGpuDrivers = gpuDriverManager.listInstalledDrivers()
            )
        }
    }

    fun loadAppReleaseHistory(showErrors: Boolean = true, forceRefresh: Boolean = false) {
        if (_uiState.value.appUpdate.historyLoading) return
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(
                appUpdate = _uiState.value.appUpdate.copy(
                    historyLoading = true,
                    historyErrorMessage = null
                )
            )
            runCatching {
                appUpdateRepository.loadReleaseHistory(forceRefresh = forceRefresh)
            }.onSuccess { releases ->
                _uiState.value = _uiState.value.copy(
                    appUpdate = _uiState.value.appUpdate.copy(
                        releaseHistory = releases,
                        historyLoading = false,
                        historyErrorMessage = null
                    )
                )
            }.onFailure { error ->
                _uiState.value = _uiState.value.copy(
                    appUpdate = _uiState.value.appUpdate.copy(
                        historyLoading = false,
                        historyErrorMessage = if (showErrors) error.message ?: "Could not load release history" else null
                    )
                )
            }
        }
    }
}
