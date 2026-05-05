package com.sbro.emucorev.ui.settings

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.core.AppUpdateRelease
import com.sbro.emucorev.core.AppUpdateRepository
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.core.GpuDriverCatalogRepository
import com.sbro.emucorev.core.GpuDriverManager
import com.sbro.emucorev.core.InstalledGpuDriver
import com.sbro.emucorev.core.RemoteGpuDriver
import com.sbro.emucorev.core.VitaCoreConfig
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.data.AppLanguage
import com.sbro.emucorev.data.AppPreferences
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class SettingsUiState(
    val packagesFolderPath: String? = null,
    val storagePath: String = "",
    val coreConfig: VitaCoreConfig = VitaCoreConfig(),
    val installedGpuDrivers: List<InstalledGpuDriver> = emptyList(),
    val remoteGpuDrivers: List<RemoteGpuDriver> = emptyList(),
    val gpuDriverCatalogLoading: Boolean = false,
    val gpuDriverCatalogError: String? = null,
    val gpuDriverDownloads: Map<String, Float> = emptyMap(),
    val appLanguage: AppLanguage = AppLanguage.SYSTEM,
    val appUpdate: AppUpdateUiState = AppUpdateUiState()
)

data class AppUpdateUiState(
    val latestRelease: AppUpdateRelease? = null,
    val checking: Boolean = false,
    val checkedOnce: Boolean = false,
    val errorMessage: String? = null,
    val downloadProgress: Float? = null,
    val downloadedApkPath: String? = null,
    val startupDialogVisible: Boolean = false
)

class SettingsViewModel(application: Application) : AndroidViewModel(application) {

    private val preferences = AppPreferences(application)
    private val coreConfigRepository = VitaCoreConfigRepository(application)
    private val gpuDriverManager = GpuDriverManager(application)
    private val gpuDriverCatalogRepository = GpuDriverCatalogRepository(application)
    private val appUpdateRepository = AppUpdateRepository(application)
    private val initialCoreConfig = coreConfigRepository.ensureDefaultsPersisted()

    private val _uiState = MutableStateFlow(
        SettingsUiState(
            packagesFolderPath = preferences.packagesFolderDisplayName(application),
            storagePath = EmulatorStorage.vitaRoot(application).absolutePath,
            coreConfig = initialCoreConfig,
            installedGpuDrivers = gpuDriverManager.listInstalledDrivers(),
            appLanguage = preferences.appLanguage
        )
    )
    val uiState: StateFlow<SettingsUiState> = _uiState.asStateFlow()

    fun onPackagesFolderSelected(uri: Uri) {
        val context = getApplication<Application>()
        preferences.setPackagesFolder(context, uri)
        _uiState.value = _uiState.value.copy(
            packagesFolderPath = preferences.packagesFolderDisplayName(context)
        )
    }

    fun clearPackagesFolder() {
        preferences.clearPackagesFolder(getApplication())
        _uiState.value = _uiState.value.copy(packagesFolderPath = null)
    }

    fun refreshCoreSettings() {
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(
                coreConfig = coreConfigRepository.ensureDefaultsPersisted(),
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

    fun updateCoreSettings(transform: (VitaCoreConfig) -> VitaCoreConfig) {
        val updated = transform(_uiState.value.coreConfig)
        _uiState.value = _uiState.value.copy(coreConfig = updated)
        viewModelScope.launch(Dispatchers.IO) {
            coreConfigRepository.save(updated)
        }
    }

    fun installGpuDriver(uri: Uri, onComplete: (Result<String>) -> Unit) {
        viewModelScope.launch(Dispatchers.IO) {
            val result = runCatching {
                val driverName = gpuDriverManager.installFromArchive(uri)
                val updated = _uiState.value.coreConfig.copy(customDriverName = driverName)
                coreConfigRepository.save(updated)
                _uiState.value = _uiState.value.copy(
                    coreConfig = updated,
                    installedGpuDrivers = gpuDriverManager.listInstalledDrivers()
                )
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

    fun installRemoteGpuDriver(driver: RemoteGpuDriver, onComplete: (Result<String>) -> Unit) {
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
                val updated = _uiState.value.coreConfig.copy(customDriverName = driverName)
                coreConfigRepository.save(updated)
                _uiState.value = _uiState.value.copy(
                    coreConfig = updated,
                    installedGpuDrivers = gpuDriverManager.listInstalledDrivers()
                )
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

    fun checkForAppUpdates(showErrors: Boolean = true, showStartupDialog: Boolean = false) {
        if (_uiState.value.appUpdate.checking) return
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(
                appUpdate = _uiState.value.appUpdate.copy(
                    checking = true,
                    errorMessage = null
                )
            )
            runCatching {
                appUpdateRepository.checkLatestRelease()
            }.onSuccess { release ->
                val startupDialogVisible = showStartupDialog &&
                    release != null &&
                    !release.tagName.equals(preferences.skippedUpdateTag, ignoreCase = true)
                _uiState.value = _uiState.value.copy(
                    appUpdate = _uiState.value.appUpdate.copy(
                        latestRelease = release,
                        checking = false,
                        checkedOnce = true,
                        errorMessage = null,
                        startupDialogVisible = startupDialogVisible
                    )
                )
            }.onFailure { error ->
                _uiState.value = _uiState.value.copy(
                    appUpdate = _uiState.value.appUpdate.copy(
                        checking = false,
                        checkedOnce = true,
                        errorMessage = if (showErrors) error.message ?: "Could not check for updates" else null,
                        startupDialogVisible = false
                    )
                )
            }
        }
    }

    fun dismissStartupUpdateDialog() {
        _uiState.value = _uiState.value.copy(
            appUpdate = _uiState.value.appUpdate.copy(startupDialogVisible = false)
        )
    }

    fun skipStartupUpdateDialog() {
        _uiState.value.appUpdate.latestRelease?.tagName?.takeIf { it.isNotBlank() }?.let { tag ->
            preferences.skippedUpdateTag = tag
        }
        dismissStartupUpdateDialog()
    }

    fun downloadAppUpdate(onComplete: (Result<Unit>) -> Unit = {}) {
        val release = _uiState.value.appUpdate.latestRelease ?: return
        if (_uiState.value.appUpdate.downloadProgress != null) return
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(
                appUpdate = _uiState.value.appUpdate.copy(
                    downloadProgress = 0f,
                    errorMessage = null,
                    downloadedApkPath = null
                )
            )
            val result = runCatching {
                val apk = appUpdateRepository.downloadApk(release) { progress ->
                    _uiState.value = _uiState.value.copy(
                        appUpdate = _uiState.value.appUpdate.copy(downloadProgress = progress)
                    )
                }
                _uiState.value = _uiState.value.copy(
                    appUpdate = _uiState.value.appUpdate.copy(
                        downloadProgress = null,
                        downloadedApkPath = apk.absolutePath
                    )
                )
            }
            result.onFailure { error ->
                _uiState.value = _uiState.value.copy(
                    appUpdate = _uiState.value.appUpdate.copy(
                        downloadProgress = null,
                        errorMessage = error.message ?: "Could not download update"
                    )
                )
            }
            withContext(Dispatchers.Main) {
                onComplete(result)
            }
        }
    }

    fun installDownloadedAppUpdate(onComplete: (Result<Unit>) -> Unit = {}) {
        val apkPath = _uiState.value.appUpdate.downloadedApkPath ?: return
        val result = runCatching {
            appUpdateRepository.launchInstaller(java.io.File(apkPath))
        }
        result.onFailure { error ->
            _uiState.value = _uiState.value.copy(
                appUpdate = _uiState.value.appUpdate.copy(
                    errorMessage = error.message ?: "Could not open update installer"
                )
            )
        }
        onComplete(result)
    }
}
