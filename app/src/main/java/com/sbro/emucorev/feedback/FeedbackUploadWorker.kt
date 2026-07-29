package com.sbro.emucorev.feedback

import android.content.Context
import android.os.Build
import androidx.work.CoroutineWorker
import androidx.work.WorkerParameters
import com.google.firebase.auth.FirebaseAuth
import com.sbro.emucorev.BuildConfig
import com.sbro.emucorev.core.GpuHardwareProfiles
import com.sbro.emucorev.core.MobileSocNameMapper
import java.io.BufferedOutputStream
import java.io.File
import java.io.OutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.nio.charset.StandardCharsets
import java.util.Locale
import java.util.UUID
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json

class FeedbackUploadWorker(
    appContext: Context,
    workerParams: WorkerParameters
) : CoroutineWorker(appContext, workerParams) {

    override suspend fun doWork(): Result = withContext(Dispatchers.IO) {
        val submissionId = inputData.getString(KEY_SUBMISSION_ID).orEmpty()
        val directory = FeedbackUploadScheduler.submissionDirectory(applicationContext, submissionId)
            ?: return@withContext Result.failure()
        val manifestFile = FeedbackUploadScheduler.manifestFile(directory)
        val feedback = runCatching {
            Json.decodeFromString<QueuedFeedback>(manifestFile.readText())
        }.getOrElse {
            directory.deleteRecursively()
            return@withContext Result.failure()
        }
        if (!FeedbackUploadScheduler.isConfigured) return@withContext finishOrRetry(directory)

        val responseCode = runCatching { upload(directory, feedback) }.getOrElse {
            return@withContext finishOrRetry(directory)
        }
        when {
            responseCode in 200..299 -> {
                directory.deleteRecursively()
                Result.success()
            }
            responseCode == HTTP_TOO_MANY_REQUESTS || responseCode >= 500 -> finishOrRetry(directory)
            else -> {
                directory.deleteRecursively()
                Result.failure()
            }
        }
    }

    private fun finishOrRetry(directory: File): Result {
        if (runAttemptCount < MAX_RETRY_ATTEMPTS) return Result.retry()
        directory.deleteRecursively()
        return Result.failure()
    }

    private fun upload(directory: File, feedback: QueuedFeedback): Int {
        val endpoint = URL(BuildConfig.FEEDBACK_ENDPOINT)
        require(endpoint.protocol == "https")
        val boundary = "EmuCoreV-${UUID.randomUUID()}"
        val connection = (endpoint.openConnection() as HttpURLConnection).apply {
            requestMethod = "POST"
            connectTimeout = 30_000
            readTimeout = 180_000
            doOutput = true
            useCaches = false
            setChunkedStreamingMode(256 * 1024)
            setRequestProperty("Content-Type", "multipart/form-data; boundary=$boundary")
            setRequestProperty("Accept", "application/json")
            // The shared Worker deliberately keeps the EmuCoreX header contract.
            setRequestProperty("X-EmuCoreX-Key", BuildConfig.FEEDBACK_API_KEY)
            setRequestProperty("User-Agent", "EmuCoreV/${BuildConfig.VERSION_NAME} (Android)")
        }
        try {
            BufferedOutputStream(connection.outputStream).use { output ->
                writeField(output, boundary, "submissionId", feedback.id)
                writeField(output, boundary, "category", feedback.category)
                writeField(output, boundary, "gameTitle", feedback.gameTitle)
                writeField(output, boundary, "gameSerial", feedback.gameSerial)
                writeField(output, boundary, "message", feedback.message)
                writeField(output, boundary, "createdAt", feedback.createdAtEpochMs.toString())
                if (feedback.includeDiagnostics) {
                    diagnostics().forEach { (name, value) -> writeField(output, boundary, name, value) }
                }
                feedback.attachments.forEach { attachment ->
                    val file = File(directory, attachment.storedName)
                    check(file.isFile && file.parentFile?.canonicalFile == directory.canonicalFile)
                    writeFile(
                        output,
                        boundary,
                        "attachments",
                        attachment.displayName,
                        attachment.mimeType,
                        file
                    )
                }
                output.write("--$boundary--\r\n".toByteArray(StandardCharsets.UTF_8))
                output.flush()
            }
            val code = connection.responseCode
            runCatching {
                val responseStream = if (code >= 400) connection.errorStream else connection.inputStream
                responseStream?.use { it.read(ByteArray(4 * 1024)) }
            }
            return code
        } finally {
            connection.disconnect()
        }
    }

    private fun diagnostics(): Map<String, String> {
        val account = runCatching { FirebaseAuth.getInstance().currentUser }.getOrNull()
        return linkedMapOf(
            "appVersion" to "EmuCoreV ${BuildConfig.VERSION_NAME}",
            "appBuild" to BuildConfig.VERSION_CODE.toString(),
            "coreVersion" to "Vita3K core",
            "androidVersion" to "${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})",
            "device" to listOf(Build.MANUFACTURER, Build.MODEL).filter(String::isNotBlank).joinToString(" "),
            "soc" to runCatching { MobileSocNameMapper.currentDeviceName() }.getOrDefault(Build.HARDWARE),
            "hardware" to "${Build.HARDWARE} · GPU family: ${detectedGpuFamily()}",
            "abis" to Build.SUPPORTED_ABIS.joinToString(", "),
            "locale" to Locale.getDefault().toLanguageTag(),
            "accountId" to account?.uid.orEmpty()
        ).filterValues(String::isNotBlank)
    }

    private fun detectedGpuFamily(): String = when (GpuHardwareProfiles.detectHardwareProfile()) {
        GpuHardwareProfiles.ADRENO -> "Adreno"
        GpuHardwareProfiles.POWERVR -> "PowerVR"
        else -> "Mali/ARM"
    }

    private fun writeField(output: OutputStream, boundary: String, name: String, value: String) {
        output.write("--$boundary\r\n".toByteArray(StandardCharsets.UTF_8))
        output.write("Content-Disposition: form-data; name=\"$name\"\r\n\r\n".toByteArray(StandardCharsets.UTF_8))
        output.write(value.toByteArray(StandardCharsets.UTF_8))
        output.write("\r\n".toByteArray(StandardCharsets.UTF_8))
    }

    private fun writeFile(
        output: OutputStream,
        boundary: String,
        fieldName: String,
        displayName: String,
        mimeType: String,
        file: File
    ) {
        val safeName = displayName.replace('"', '_')
        output.write("--$boundary\r\n".toByteArray(StandardCharsets.UTF_8))
        output.write(
            "Content-Disposition: form-data; name=\"$fieldName\"; filename=\"$safeName\"\r\n"
                .toByteArray(StandardCharsets.UTF_8)
        )
        output.write("Content-Type: $mimeType\r\n\r\n".toByteArray(StandardCharsets.UTF_8))
        file.inputStream().buffered().use { it.copyTo(output, 256 * 1024) }
        output.write("\r\n".toByteArray(StandardCharsets.UTF_8))
    }

    companion object {
        const val KEY_SUBMISSION_ID = "submission_id"
        private const val MAX_RETRY_ATTEMPTS = 8
        private const val HTTP_TOO_MANY_REQUESTS = 429
    }
}
