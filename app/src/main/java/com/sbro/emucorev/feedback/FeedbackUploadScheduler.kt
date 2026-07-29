package com.sbro.emucorev.feedback

import android.content.Context
import androidx.work.BackoffPolicy
import androidx.work.Constraints
import androidx.work.Data
import androidx.work.ExistingWorkPolicy
import androidx.work.NetworkType
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.WorkManager
import com.sbro.emucorev.BuildConfig
import java.io.File
import java.io.IOException
import java.util.UUID
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json

object FeedbackUploadScheduler {
    private const val QUEUE_DIRECTORY = "feedback_queue"
    private const val MANIFEST_FILE = "submission.json"
    private const val WORK_TAG = "feedback_upload"

    val isConfigured: Boolean
        get() = BuildConfig.FEEDBACK_ENDPOINT.startsWith("https://") &&
            BuildConfig.FEEDBACK_API_KEY.isNotBlank()

    suspend fun enqueue(
        context: Context,
        category: String,
        gameTitle: String,
        gameSerial: String,
        message: String,
        includeDiagnostics: Boolean,
        attachments: List<FeedbackAttachment>
    ): UUID = withContext(Dispatchers.IO) {
        check(isConfigured) { "Feedback endpoint is not configured." }
        require(message.isNotBlank())
        require(message.length <= FeedbackLimits.MAX_MESSAGE_LENGTH)
        require(gameTitle.length <= FeedbackLimits.MAX_GAME_LENGTH)
        require(gameSerial.length <= 64)
        require(attachments.size <= FeedbackLimits.MAX_ATTACHMENTS)

        val submissionId = UUID.randomUUID().toString()
        val submissionDirectory = File(context.noBackupFilesDir, "$QUEUE_DIRECTORY/$submissionId")
        check(submissionDirectory.mkdirs()) { "Could not create the feedback queue." }

        try {
            var totalBytes = 0L
            val queuedAttachments = attachments.mapIndexed { index, attachment ->
                val storedName = "attachment_${index + 1}"
                val destination = File(submissionDirectory, storedName)
                val copiedBytes = copyWithLimits(
                    context = context,
                    attachment = attachment,
                    destination = destination,
                    currentTotalBytes = totalBytes
                )
                totalBytes += copiedBytes
                QueuedFeedbackAttachment(
                    storedName = storedName,
                    displayName = sanitizeDisplayName(attachment.displayName),
                    mimeType = attachment.mimeType.take(160),
                    sizeBytes = copiedBytes
                )
            }
            val queuedFeedback = QueuedFeedback(
                id = submissionId,
                category = category.take(64),
                gameTitle = gameTitle.trim().take(FeedbackLimits.MAX_GAME_LENGTH),
                gameSerial = gameSerial.trim().take(64),
                message = message.trim().take(FeedbackLimits.MAX_MESSAGE_LENGTH),
                includeDiagnostics = includeDiagnostics,
                createdAtEpochMs = System.currentTimeMillis(),
                attachments = queuedAttachments
            )
            File(submissionDirectory, MANIFEST_FILE).writeText(Json.encodeToString(queuedFeedback))

            val request = OneTimeWorkRequestBuilder<FeedbackUploadWorker>()
                .setInputData(Data.Builder().putString(FeedbackUploadWorker.KEY_SUBMISSION_ID, submissionId).build())
                .setConstraints(Constraints.Builder().setRequiredNetworkType(NetworkType.CONNECTED).build())
                .setBackoffCriteria(BackoffPolicy.EXPONENTIAL, 15, TimeUnit.SECONDS)
                .addTag(WORK_TAG)
                .build()
            WorkManager.getInstance(context).enqueueUniqueWork(
                "feedback_upload_$submissionId",
                ExistingWorkPolicy.KEEP,
                request
            )
            request.id
        } catch (error: Throwable) {
            submissionDirectory.deleteRecursively()
            throw error
        }
    }

    internal fun submissionDirectory(context: Context, id: String): File? {
        if (!id.matches(Regex("[0-9a-fA-F-]{36}"))) return null
        return File(context.noBackupFilesDir, "$QUEUE_DIRECTORY/$id")
    }

    internal fun manifestFile(directory: File): File = File(directory, MANIFEST_FILE)

    private fun copyWithLimits(
        context: Context,
        attachment: FeedbackAttachment,
        destination: File,
        currentTotalBytes: Long
    ): Long {
        var copied = 0L
        val input = context.contentResolver.openInputStream(attachment.uri)
            ?: throw IOException("The attachment cannot be opened.")
        input.use { source ->
            destination.outputStream().buffered().use { target ->
                val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                while (true) {
                    val read = source.read(buffer)
                    if (read < 0) break
                    copied += read
                    if (copied > FeedbackLimits.MAX_ATTACHMENT_BYTES) {
                        throw IOException("An attachment exceeds the size limit.")
                    }
                    if (currentTotalBytes + copied > FeedbackLimits.MAX_TOTAL_BYTES) {
                        throw IOException("Attachments exceed the total size limit.")
                    }
                    target.write(buffer, 0, read)
                }
            }
        }
        return copied
    }

    private fun sanitizeDisplayName(value: String): String = value
        .replace(Regex("[\\r\\n\\t\\u0000-\\u001f]"), "_")
        .replace('/', '_')
        .replace('\\', '_')
        .trim()
        .take(120)
        .ifBlank { "attachment" }
}
