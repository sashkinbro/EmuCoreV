package com.sbro.emucorev.feedback

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import kotlinx.serialization.Serializable

object FeedbackLimits {
    const val MAX_ATTACHMENTS = 5
    const val MAX_ATTACHMENT_BYTES = 49L * 1024L * 1024L
    const val MAX_TOTAL_BYTES = 90L * 1024L * 1024L
    const val MAX_MESSAGE_LENGTH = 3_000
    const val MAX_GAME_LENGTH = 160
}

data class FeedbackAttachment(
    val uri: Uri,
    val displayName: String,
    val mimeType: String,
    val sizeBytes: Long?
)

sealed class FeedbackAttachmentError {
    data object TooMany : FeedbackAttachmentError()
    data object ItemTooLarge : FeedbackAttachmentError()
    data object TotalTooLarge : FeedbackAttachmentError()
    data object Unreadable : FeedbackAttachmentError()
}

data class FeedbackAttachmentInspection(
    val attachments: List<FeedbackAttachment> = emptyList(),
    val error: FeedbackAttachmentError? = null
)

object FeedbackAttachmentInspector {
    fun inspect(context: Context, uris: List<Uri>): FeedbackAttachmentInspection {
        val uniqueUris = uris.distinct()
        if (uniqueUris.size > FeedbackLimits.MAX_ATTACHMENTS) {
            return FeedbackAttachmentInspection(error = FeedbackAttachmentError.TooMany)
        }
        var knownTotal = 0L
        val attachments = ArrayList<FeedbackAttachment>(uniqueUris.size)
        for (uri in uniqueUris) {
            val metadata = readMetadata(context, uri)
                ?: return FeedbackAttachmentInspection(error = FeedbackAttachmentError.Unreadable)
            if (metadata.sizeBytes != null && metadata.sizeBytes > FeedbackLimits.MAX_ATTACHMENT_BYTES) {
                return FeedbackAttachmentInspection(error = FeedbackAttachmentError.ItemTooLarge)
            }
            knownTotal += metadata.sizeBytes ?: 0L
            if (knownTotal > FeedbackLimits.MAX_TOTAL_BYTES) {
                return FeedbackAttachmentInspection(error = FeedbackAttachmentError.TotalTooLarge)
            }
            attachments += metadata
        }
        return FeedbackAttachmentInspection(attachments = attachments)
    }

    private fun readMetadata(context: Context, uri: Uri): FeedbackAttachment? {
        var name: String? = null
        var size: Long? = null
        runCatching {
            context.contentResolver.query(
                uri,
                arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE),
                null,
                null,
                null
            )?.use { cursor ->
                if (cursor.moveToFirst()) {
                    cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME).takeIf { it >= 0 }?.let { index ->
                        name = cursor.getString(index)
                    }
                    cursor.getColumnIndex(OpenableColumns.SIZE).takeIf { it >= 0 }?.let { index ->
                        if (!cursor.isNull(index)) size = cursor.getLong(index).takeIf { it >= 0L }
                    }
                }
            }
        }
        if (size == null) {
            size = runCatching {
                context.contentResolver.openAssetFileDescriptor(uri, "r")?.use { descriptor ->
                    descriptor.length.takeIf { it >= 0L }
                }
            }.getOrNull()
        }
        val readable = runCatching {
            context.contentResolver.openInputStream(uri)?.use { true } ?: false
        }.getOrDefault(false)
        if (!readable) return null

        val fallbackName = uri.lastPathSegment?.substringAfterLast('/')?.takeIf(String::isNotBlank)
            ?: "attachment"
        return FeedbackAttachment(
            uri = uri,
            displayName = name?.takeIf(String::isNotBlank) ?: fallbackName,
            mimeType = context.contentResolver.getType(uri)?.takeIf(String::isNotBlank)
                ?: "application/octet-stream",
            sizeBytes = size
        )
    }
}

@Serializable
internal data class QueuedFeedback(
    val id: String,
    val category: String,
    val gameTitle: String,
    val gameSerial: String,
    val message: String,
    val includeDiagnostics: Boolean,
    val createdAtEpochMs: Long,
    val attachments: List<QueuedFeedbackAttachment>
)

@Serializable
internal data class QueuedFeedbackAttachment(
    val storedName: String,
    val displayName: String,
    val mimeType: String,
    val sizeBytes: Long
)
