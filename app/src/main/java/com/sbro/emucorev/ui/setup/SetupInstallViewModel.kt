package com.sbro.emucorev.ui.setup

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.R
import com.sbro.emucorev.core.DocumentPathResolver
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.core.InstallStateBus
import com.sbro.emucorev.core.NativeInstallProgress
import com.sbro.emucorev.core.VitaArchiveInspection
import com.sbro.emucorev.core.VitaArchiveInspector
import com.sbro.emucorev.core.VitaArchiveRepacker
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
    License,
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
    private var nativeProgressOffset = 0f
    private var nativeProgressScale = 1f
    private val stagedFilesToCleanup = mutableListOf<String>()

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
            val path = resolveInstallSource(uriString)
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

    fun installContent(uriString: String, repairArchive: Boolean = false) {
        runInstall(InstallOperation.Content) {
            val path = resolveInstallSource(uriString)
            if (path == null) {
                finishError(appContext.getString(R.string.install_dialog_content_failed))
                return@runInstall
            }
            val inspection = VitaArchiveInspector.inspect(path)
            Log.i(
                TAG,
                "Content install selected repair=$repairArchive path=$path readable=${inspection.readable} " +
                    "metadata=${inspection.hasInstallMetadata} vitamin=${inspection.vitaminDump} " +
                    "unsupported=${inspection.unsupportedCompressionEntries}"
            )
            val installPath = if (repairArchive) {
                val repairedPath = repairArchiveForInstall(path) ?: run {
                    _uiState.value = _uiState.value.copy(detail = null)
                    finishError(
                        message = appContext.getString(R.string.install_dialog_content_failed),
                        fallbackDetail = archiveFailureDetail(inspection, repairRequested = true)
                    )
                    return@runInstall
                }
                val repairedInspection = VitaArchiveInspector.inspect(repairedPath)
                Log.i(
                    TAG,
                    "Content repair result path=$repairedPath readable=${repairedInspection.readable} " +
                        "metadata=${repairedInspection.hasInstallMetadata} vitamin=${repairedInspection.vitaminDump} " +
                        "unsupported=${repairedInspection.unsupportedCompressionEntries}"
                )
                if (
                    !repairedInspection.readable ||
                    !repairedInspection.hasInstallMetadata ||
                    !repairedInspection.supportedCompression
                ) {
                    _uiState.value = _uiState.value.copy(detail = null)
                    finishError(
                        message = appContext.getString(R.string.install_dialog_content_failed),
                        fallbackDetail = archiveFailureDetail(repairedInspection, repairRequested = true)
                    )
                    return@runInstall
                }
                repairedPath
            } else {
                path
            }
            _uiState.value = _uiState.value.copy(detail = null)
            val installedCount = VitaInstallBridge.installContent(appContext, installPath, systemLanguage())
            Log.i(TAG, "Content install finished repair=$repairArchive installedCount=$installedCount installPath=$installPath")
            if (installedCount > 0) {
                finishSuccess(
                    appContext.resources.getQuantityString(
                        R.plurals.install_dialog_content_done,
                        1,
                        1
                    )
                )
            } else {
                finishError(
                    message = appContext.getString(R.string.install_dialog_content_failed),
                    fallbackDetail = archiveFailureDetail(inspection, repairRequested = repairArchive)
                )
            }
        }
    }

    fun installLicense(uriString: String) {
        runInstall(InstallOperation.License) {
            val path = resolveInstallSource(uriString)
            if (path == null) {
                finishError(appContext.getString(R.string.install_dialog_license_failed))
                return@runInstall
            }
            val success = VitaInstallBridge.installLicense(appContext, path, systemLanguage())
            if (success) {
                finishSuccess(appContext.getString(R.string.install_dialog_license_done))
            } else {
                finishError(appContext.getString(R.string.install_dialog_license_failed))
            }
        }
    }

    fun installPkg(uriString: String, zrif: String) {
        runInstall(InstallOperation.Pkg) {
            val path = resolveInstallSource(uriString)
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
        nativeProgressOffset = 0f
        nativeProgressScale = 1f
        stagedFilesToCleanup.clear()
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
                // Staged payloads are game-sized. Always remove them, including
                // on failure, otherwise every install permanently leaks a full
                // copy of the source package.
                stagedFilesToCleanup.forEach { staged ->
                    DocumentPathResolver.cleanupStagedFile(appContext, staged)
                }
                stagedFilesToCleanup.clear()
                VitaInstallBridge.setListener(null)
                nativeProgressOffset = 0f
                nativeProgressScale = 1f
            }
        }
    }

    /**
     * Resolves an install source and marks any staged copy for cleanup once the
     * operation finishes.
     */
    private fun resolveInstallSource(uriString: String): String? {
        val path = DocumentPathResolver.resolveFilePath(appContext, uriString, copyToCache = true)
        if (path != null) {
            stagedFilesToCleanup.add(path)
        }
        return path
    }

    private fun handleProgress(progress: NativeInstallProgress) {
        val current = progress.current.takeIf { it > 0f }?.roundToInt()
        val total = progress.total.takeIf { it > 0f }?.roundToInt()
        val scaledProgress = nativeProgressOffset + progress.progress.coerceIn(0f, 100f) * nativeProgressScale
        _uiState.value = _uiState.value.copy(
            progress = scaledProgress.coerceIn(0f, 100f),
            current = current,
            total = total,
            detail = progress.detail?.takeIf { it.isNotBlank() }
        )
    }

    private fun repairArchiveForInstall(path: String): String? {
        if (!VitaArchiveRepacker.canRepack(path)) return null
        _uiState.value = _uiState.value.copy(
            progress = 0f,
            current = null,
            total = null,
            detail = appContext.getString(R.string.install_dialog_repack_preparing)
        )
        return VitaArchiveRepacker.repackToInstallZip(
            sourcePath = path,
            cacheRoot = EmulatorStorage.installStagingRoot(appContext)
        ) { progress ->
            _uiState.value = _uiState.value.copy(
                progress = (progress.progress * REPAIR_REPACK_PROGRESS_SCALE).coerceIn(0f, REPAIR_NATIVE_PROGRESS_OFFSET),
                current = progress.current,
                total = progress.total,
                detail = progress.detail?.let {
                    appContext.getString(R.string.install_dialog_repack_entry, it)
                } ?: appContext.getString(R.string.install_dialog_repack_preparing)
            )
        }?.absolutePath?.also {
            stagedFilesToCleanup.add(it)
            nativeProgressOffset = REPAIR_NATIVE_PROGRESS_OFFSET
            nativeProgressScale = REPAIR_NATIVE_PROGRESS_SCALE
            _uiState.value = _uiState.value.copy(
                progress = REPAIR_NATIVE_PROGRESS_OFFSET,
                current = null,
                total = null,
                detail = appContext.getString(R.string.install_dialog_repack_installing)
            )
        }
    }

    private fun archiveFailureDetail(
        inspection: VitaArchiveInspection,
        repairRequested: Boolean
    ): String {
        return when {
            !inspection.readable -> inspection.errorMessage
                ?: appContext.getString(R.string.install_dialog_archive_unreadable)
            !inspection.hasInstallMetadata -> appContext.getString(R.string.install_dialog_archive_missing_metadata)
            !inspection.supportedCompression -> appContext.resources.getQuantityString(
                R.plurals.install_dialog_archive_unsupported_compression,
                inspection.unsupportedCompressionEntries,
                inspection.unsupportedCompressionEntries
            )
            repairRequested -> appContext.getString(R.string.install_dialog_repack_failed)
            else -> appContext.getString(R.string.install_dialog_content_try_repair)
        }
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

    private companion object {
        const val TAG = "SetupInstallViewModel"
        const val REPAIR_NATIVE_PROGRESS_OFFSET = 45f
        const val REPAIR_NATIVE_PROGRESS_SCALE = 0.55f
        const val REPAIR_REPACK_PROGRESS_SCALE = 0.45f
    }
}
