package com.sbro.emucorev.core

import android.content.ContentValues
import android.content.Context
import android.os.Environment
import android.provider.MediaStore
import android.util.Log
import com.sbro.emucorev.core.vita.Emulator
import java.io.File
import java.io.IOException
import java.util.Locale
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

/**
 * Screenshots: the core reads back the frame and encodes it (seconds at high resolutions), so
 * [capture] must run off the UI thread. The encoded file is then published into
 * MediaStore (Pictures/EmuCoreV) so it shows up in the gallery.
 */
object ScreenshotPublisher {
    private const val TAG = "ScreenshotPublisher"
    private const val ALBUM = "EmuCoreV"
    private val sequence = AtomicInteger()
    private val inFlight = AtomicBoolean(false)

    /** Millisecond timestamp plus a process-wide sequence: two shots never share a name. */
    internal fun displayName(timestampMs: Long, sequence: Int, extension: String): String {
        val time = java.text.SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(java.util.Date(timestampMs))
        val millis = (timestampMs % 1000).toInt().coerceAtLeast(0)
        val ext = extension.trimStart('.').lowercase(Locale.US).ifBlank { "png" }
        return String.format(Locale.US, "EmuCoreV_%s-%03d-%d.%s", time, millis, sequence, ext)
    }

    internal fun mimeTypeFor(extension: String): String =
        when (extension.trimStart('.').lowercase(Locale.US)) {
            "jpg", "jpeg" -> "image/jpeg"
            else -> "image/png"
        }

    /** Blocking. Returns true only when a screenshot was captured and published. */
    fun capture(context: Context, emulator: Emulator): Boolean {
        if (!inFlight.compareAndSet(false, true)) return false
        try {
            val path = emulator.requestScreenshot() ?: return false
            val file = File(path)
            if (!file.isFile) return false
            val published = publish(context, file)
            // The MediaStore copy is the user-visible one; keep the app copy only if publishing failed.
            if (published) runCatching { file.delete() }
            return published
        } catch (error: Exception) {
            Log.e(TAG, "Screenshot failed", error)
            return false
        } finally {
            inFlight.set(false)
        }
    }

    private fun publish(context: Context, source: File): Boolean {
        val resolver = context.contentResolver
        val extension = source.extension
        val values = ContentValues().apply {
            put(MediaStore.Images.Media.DISPLAY_NAME, displayName(System.currentTimeMillis(), sequence.getAndIncrement(), extension))
            put(MediaStore.Images.Media.MIME_TYPE, mimeTypeFor(extension))
            put(MediaStore.Images.Media.RELATIVE_PATH, "${Environment.DIRECTORY_PICTURES}/$ALBUM")
            put(MediaStore.Images.Media.IS_PENDING, 1)
        }
        val uri = resolver.insert(MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY), values)
            ?: return false
        return try {
            val output = resolver.openOutputStream(uri) ?: throw IOException("No output stream for $uri")
            output.use { out -> source.inputStream().use { it.copyTo(out) } }
            val done = ContentValues().apply { put(MediaStore.Images.Media.IS_PENDING, 0) }
            resolver.update(uri, done, null, null)
            true
        } catch (error: Exception) {
            Log.e(TAG, "Failed to publish screenshot to MediaStore", error)
            runCatching { resolver.delete(uri, null, null) }
            false
        }
    }
}
