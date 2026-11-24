package com.sbro.emucorev.ui.onboarding

import android.app.Application
import android.os.Environment
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.core.FirmwareDownloadException
import com.sbro.emucorev.core.FirmwareDownloader
import com.sbro.emucorev.core.FirmwareKind
import com.sbro.emucorev.core.FirmwareSource
import com.sbro.emucorev.core.FirmwareSources
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

enum class FirmwareDownloadStatus {
    Idle,
    Running,
    Completed,
    Failed
}

data class FirmwareDownloadState(
    val status: FirmwareDownloadStatus = FirmwareDownloadStatus.Idle,
    val kind: FirmwareKind? = null,
    val progress: Float = 0f,
    val bytesDownloaded: Long = 0L,
    val totalBytes: Long = 0L,
    val resultFileUri: String? = null,
    val errorMessage: String? = null,
    /** The file arrived but failed validation (truncated / not a PUP / wrong hash), as opposed to a network error. */
    val corruptDownload: Boolean = false
)

class FirmwareDownloadViewModel(application: Application) : AndroidViewModel(application) {

    private val appContext = getApplication<Application>()
    private val _state = MutableStateFlow(FirmwareDownloadState())
    val state: StateFlow<FirmwareDownloadState> = _state.asStateFlow()

    private var currentJob: Job? = null

    fun start(kind: FirmwareKind) {
        if (_state.value.status == FirmwareDownloadStatus.Running) return
        val source = FirmwareSources.forKind(kind)
        _state.value = FirmwareDownloadState(
            status = FirmwareDownloadStatus.Running,
            kind = kind,
            totalBytes = source.sizeBytes
        )
        currentJob = viewModelScope.launch {
            try {
                val file = download(source)
                withContext(Dispatchers.Main) {
                    _state.value = _state.value.copy(
                        status = FirmwareDownloadStatus.Completed,
                        progress = 1f,
                        bytesDownloaded = file.length(),
                        totalBytes = file.length(),
                        resultFileUri = "file://${file.absolutePath}"
                    )
                }
            } catch (error: CancellationException) {
                throw error
            } catch (error: Throwable) {
                Log.e(TAG, "Firmware download failed", error)
                withContext(Dispatchers.Main) {
                    _state.value = _state.value.copy(
                        status = FirmwareDownloadStatus.Failed,
                        errorMessage = error.message,
                        corruptDownload = (error as? FirmwareDownloadException)?.isCorruptDownload == true
                    )
                }
            } finally {
                currentJob = null
            }
        }
    }

    fun cancel() {
        currentJob?.cancel()
        currentJob = null
        _state.value = FirmwareDownloadState()
    }

    fun consumeResult() {
        _state.value = FirmwareDownloadState()
    }

    private suspend fun download(source: FirmwareSource): File = withContext(Dispatchers.IO) {
        val dir = File(
            appContext.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS) ?: appContext.cacheDir,
            "firmware-downloads"
        )
        val job = currentCoroutineContext()[Job]
        try {
            FirmwareDownloader().download(
                source = source,
                directory = dir,
                isCancelled = { job?.isActive == false }
            ) { downloaded, total ->
                _state.value = _state.value.copy(
                    bytesDownloaded = downloaded,
                    totalBytes = total,
                    progress = if (total > 0) (downloaded.toFloat() / total).coerceIn(0f, 1f) else 0f
                )
            }
        } catch (error: FirmwareDownloadException) {
            if (error.reason == FirmwareDownloadException.Reason.CANCELLED) throw CancellationException("Firmware download cancelled")
            throw error
        }
    }

    private companion object {
        private const val TAG = "FirmwareDownload"
    }
}
