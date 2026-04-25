package com.sbro.emucorev.ui.setup

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.R
import com.sbro.emucorev.core.DocumentPathResolver
import com.sbro.emucorev.core.InstallStateBus
import com.sbro.emucorev.core.NativeInstallProgress
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.core.VitaInstallBridge
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlin.math.roundToInt

enum class InstallOperation {
    Firmware,
    Content,
    Pkg
}

enum class InstallStatus {
    Idle,
    Running,
    Success,
    Error
}

data class SetupInstallUiState(
    val status: InstallStatus = InstallStatus.Idle,
    val operation: InstallOperation? = null,
    val progress: Float = 0f,
    val current: Int? = null,
    val total: Int? = null,
    val detail: String? = null,
    val message: String? = null
) {
    val visible: Boolean
        get() = status != InstallStatus.Idle
}

class SetupInstallViewModel(application: Application) : AndroidViewModel(application) {
    private val appContext = getApplication<Application>()
    private val configRepository = VitaCoreConfigRepository(appContext)
    private val _uiState = MutableStateFlow(SetupInstallUiState())
    val uiState: StateFlow<SetupInstallUiState> = _uiState.asStateFlow()

    init {
        viewModelScope.launch(Dispatchers.IO) {
            val config = configRepository.ensureDefaultsPersisted()
            if (config.showLiveAreaScreen) {
                configRepository.save(config.copy(showLiveAreaScreen = false))
            }
        }
    }

    fun dismissDialog() {
        if (_uiState.value.status == InstallStatus.Running) return
        _uiState.value = SetupInstallUiState()
    }

    fun installFirmware(uriString: String) {
        runInstall(InstallOperation.Firmware) {
            val path = DocumentPathResolver.resolveFilePath(appContext, uriString, copyToCache = true)
            if (path == null) {
                finishError(appContext.getString(R.string.install_dialog_firmware_failed))
                return@runInstall
            }
            val version = VitaInstallBridge.installFirmware(appContext, path, systemLanguage())
            if (version != null) {
                finishSuccess(appContext.getString(R.string.install_dialog_firmware_done))
            } else {
                finishError(appContext.getString(R.string.install_dialog_firmware_failed))
            }
        }
    }

    fun installContent(uriString: String) {
        runInstall(InstallOperation.Content) {
            val path = DocumentPathResolver.resolveFilePath(appContext, uriString, copyToCache = true)
            if (path == null) {
                finishError(appContext.getString(R.string.install_dialog_content_failed))
                return@runInstall
            }
            val installedCount = VitaInstallBridge.installContent(appContext, path, systemLanguage())
            if (installedCount > 0) {
                finishSuccess(
                    appContext.resources.getQuantityString(
                        R.plurals.install_dialog_content_done,
                        installedCount,
                        installedCount
                    )
                )
            } else {
                finishError(appContext.getString(R.string.install_dialog_content_failed))
            }
        }
    }

    fun installPkg(uriString: String, zrif: String) {
        runInstall(InstallOperation.Pkg) {
            val path = DocumentPathResolver.resolveFilePath(appContext, uriString, copyToCache = true)
            if (path == null) {
                finishError(appContext.getString(R.string.install_dialog_pkg_failed))
                return@runInstall
            }
            val success = VitaInstallBridge.installPkg(appContext, path, zrif, systemLanguage())
            if (success) {
                finishSuccess(appContext.getString(R.string.install_dialog_pkg_done))
            } else {
                finishError(appContext.getString(R.string.install_dialog_pkg_failed))
            }
        }
    }

    private fun runInstall(
        operation: InstallOperation,
        block: () -> Unit
    ) {
        if (_uiState.value.status == InstallStatus.Running) return
        _uiState.value = SetupInstallUiState(
            status = InstallStatus.Running,
            operation = operation
        )

        viewModelScope.launch(Dispatchers.IO) {
            VitaInstallBridge.setListener { progress -> handleProgress(progress) }
            try {
                block()
            } catch (error: Throwable) {
                finishError(
                    message = appContext.getString(R.string.install_dialog_unexpected_error),
                    fallbackDetail = error.message
                )
            } finally {
                VitaInstallBridge.setListener(null)
            }
        }
    }

    private fun handleProgress(progress: NativeInstallProgress) {
        val current = progress.current.takeIf { it > 0f }?.roundToInt()
        val total = progress.total.takeIf { it > 0f }?.roundToInt()
        _uiState.value = _uiState.value.copy(
            progress = progress.progress.coerceIn(0f, 100f),
            current = current,
            total = total,
            detail = progress.detail?.takeIf { it.isNotBlank() }
        )
    }

    private fun finishSuccess(message: String) {
        InstallStateBus.notifyCompleted()
        _uiState.value = _uiState.value.copy(
            status = InstallStatus.Success,
            progress = 100f,
            current = null,
            total = null,
            detail = null,
            message = message
        )
    }

    private fun finishError(
        message: String,
        fallbackDetail: String? = null
    ) {
        val detailText = _uiState.value.detail?.takeIf { it.isNotBlank() }
            ?: fallbackDetail?.takeIf { it.isNotBlank() }
        _uiState.value = _uiState.value.copy(
            status = InstallStatus.Error,
            current = null,
            total = null,
            message = message,
            detail = detailText
        )
    }

    private fun systemLanguage(): Int = configRepository.load().sysLang
}
