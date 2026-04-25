package com.sbro.emucorev.ui.settings

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
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
    val gpuDriverDownloadId: String? = null,
    val gpuDriverDownloadProgress: Float = 0f,
    val appLanguage: AppLanguage = AppLanguage.SYSTEM
)

class SettingsViewModel(application: Application) : AndroidViewModel(application) {

    private val preferences = AppPreferences(application)
    private val coreConfigRepository = VitaCoreConfigRepository(application)
    private val gpuDriverManager = GpuDriverManager(application)
    private val gpuDriverCatalogRepository = GpuDriverCatalogRepository(application)
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
        if (_uiState.value.gpuDriverDownloadId != null) return
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(
                gpuDriverDownloadId = driver.id,
                gpuDriverDownloadProgress = 0f
            )
            val result = runCatching {
                val archive = gpuDriverCatalogRepository.downloadDriver(driver) { progress ->
                    _uiState.value = _uiState.value.copy(gpuDriverDownloadProgress = progress)
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
                gpuDriverDownloadId = null,
                gpuDriverDownloadProgress = 0f
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
}
