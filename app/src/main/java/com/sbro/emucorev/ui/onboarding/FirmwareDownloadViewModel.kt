package com.sbro.emucorev.ui.onboarding

import android.app.Application
import android.os.Environment
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.core.FirmwareKind
import com.sbro.emucorev.core.FirmwareSources
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL

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
    val errorMessage: String? = null
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
            totalBytes = source.approximateSizeBytes
        )
        currentJob = viewModelScope.launch {
            try {
                val file = download(source.url, source.fileName)
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
                        errorMessage = error.message
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

    private suspend fun download(urlString: String, fileName: String): File = withContext(Dispatchers.IO) {
        val dir = File(
            appContext.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS) ?: appContext.cacheDir,
            "firmware-downloads"
        ).apply { mkdirs() }
        val outFile = File(dir, fileName)
        val partFile = File(dir, "$fileName.part")
        if (outFile.exists()) outFile.delete()
        if (partFile.exists()) partFile.delete()

        val connection = URL(urlString).openConnection() as HttpURLConnection
        try {
            connection.connectTimeout = 20_000
            connection.readTimeout = 30_000
            connection.requestMethod = "GET"
            connection.instanceFollowRedirects = true
            connection.setRequestProperty("User-Agent", "EmuCoreV Android")
            connection.connect()

            val responseCode = connection.responseCode
            if (responseCode !in 200..299) {
                throw RuntimeException("HTTP $responseCode")
            }

            val fallbackSize = _state.value.kind?.let(FirmwareSources::forKind)?.approximateSizeBytes
                ?: FirmwareSources.update.approximateSizeBytes
            val totalLength = connection.contentLengthLong.takeIf { it > 0 } ?: fallbackSize

            _state.value = _state.value.copy(totalBytes = totalLength)

            connection.inputStream.use { input ->
                FileOutputStream(partFile).use { output ->
                    val buffer = ByteArray(64 * 1024)
                    var total = 0L
                    var lastEmitted = 0L
                    while (true) {
                        currentCoroutineContext().ensureActive()
                        val read = input.read(buffer)
                        if (read <= 0) break
                        output.write(buffer, 0, read)
                        total += read
                        if (total - lastEmitted > 512 * 1024) {
                            lastEmitted = total
                            val progress = if (totalLength > 0) total.toFloat() / totalLength else 0f
                            _state.value = _state.value.copy(
                                bytesDownloaded = total,
                                totalBytes = totalLength,
                                progress = progress.coerceIn(0f, 1f)
                            )
                        }
                    }
                    output.flush()
                }
            }
            if (!partFile.renameTo(outFile)) {
                throw RuntimeException("Could not finalize firmware download")
            }
            return@withContext outFile
        } catch (error: CancellationException) {
            partFile.delete()
            throw error
        } catch (error: Throwable) {
            partFile.delete()
            throw error
        } finally {
            connection.disconnect()
        }
    }

    private companion object {
        private const val TAG = "FirmwareDownload"
    }
}
