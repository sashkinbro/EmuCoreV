package com.sbro.emucorev.core

import android.content.Context
import android.util.Log
import java.io.File
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.CancellationException
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

data class SaveStateSlot(
    val slot: Int,
    val exists: Boolean,
    val statePath: String?,
    val thumbnailPath: String?,
    val sizeBytes: Long,
    val timestamp: Long,
    val sessionMatch: Boolean,
    val engineVersion: Int
) {
    val isQuick: Boolean get() = slot == SaveStateRepository.QUICK_SLOT
}

internal interface SaveStateBackend {
    fun save(path: String, appVersion: String): SaveStateResult
    fun load(path: String, allowCrossSession: Boolean): SaveStateResult
    fun inspect(path: String): SaveStateResult
    fun delete(path: String): SaveStateResult
    fun captureThumbnail(path: String): Boolean
}

private object NativeSaveStateBackend : SaveStateBackend {
    override fun save(path: String, appVersion: String) = SaveStateBridge.save(path, appVersion)
    override fun load(path: String, allowCrossSession: Boolean) = SaveStateBridge.load(path, allowCrossSession)
    override fun inspect(path: String) = SaveStateBridge.inspect(path)
    override fun delete(path: String) = SaveStateBridge.delete(path)
    override fun captureThumbnail(path: String) = SaveStateBridge.captureThumbnail(path)
}

class SaveStateRepository internal constructor(
    private val context: Context,
    private val backend: SaveStateBackend = NativeSaveStateBackend
) {

    companion object {
        const val QUICK_SLOT = 0
        const val FIRST_SLOT = 1
        const val LAST_SLOT = 5
        val SLOT_ORDER = listOf(QUICK_SLOT, 1, 2, 3, 4, 5)

        private const val DIRECTORY = "savestates"
        private const val STATE_EXTENSION = ".ecvs"
        private const val THUMBNAIL_EXTENSION = ".jpg"
        private val operationLock = ReentrantLock()
    }

    fun slotsDirectory(titleId: String): File =
        File(EmulatorStorage.runtimeRoot(context), "$DIRECTORY/${sanitizeTitleId(titleId)}").apply { mkdirs() }

    fun gamesWithSlots(): List<String> = operationLock.withLock {
        val root = File(EmulatorStorage.runtimeRoot(context), DIRECTORY)
        if (!root.isDirectory) return@withLock emptyList()
        root.listFiles().orEmpty()
            .filter { it.isDirectory }
            .map { it.name }
            .sorted()
    }

    fun stateFile(titleId: String, slot: Int): File =
        File(slotsDirectory(titleId), baseName(slot) + STATE_EXTENSION)

    fun thumbnailFile(titleId: String, slot: Int): File =
        File(slotsDirectory(titleId), baseName(slot) + THUMBNAIL_EXTENSION)

    fun listSlots(titleId: String): List<SaveStateSlot> = operationLock.withLock {
        if (titleId.isBlank()) return@withLock emptyList()
        SLOT_ORDER.map { slot ->
            val state = stateFile(titleId, slot)
            val thumbnail = thumbnailFile(titleId, slot)
            val exists = state.isFile && state.length() > 0
            val inspection = if (exists) backend.inspect(state.absolutePath) else null
            SaveStateSlot(
                slot = slot,
                exists = exists,
                statePath = state.absolutePath.takeIf { exists },
                thumbnailPath = thumbnail.absolutePath.takeIf { thumbnail.isFile && thumbnail.length() > 0 },
                sizeBytes = if (exists) state.length() else 0L,
                timestamp = if (exists) state.lastModified() else 0L,
                sessionMatch = inspection?.sessionMatch ?: true,
                engineVersion = inspection?.engineVersion ?: 0
            )
        }
    }

    fun slot(titleId: String, slot: Int): SaveStateSlot =
        listSlots(titleId).firstOrNull { it.slot == slot }
            ?: SaveStateSlot(slot, false, null, null, 0L, 0L, true, 0)

    fun latestSlot(titleId: String): Int? =
        listSlots(titleId)
            .filter { it.exists }
            .maxByOrNull { it.timestamp }
            ?.slot

    fun save(titleId: String, slot: Int, appVersion: String, captureThumbnail: Boolean = true): SaveStateResult =
        operationLock.withLock {
            var pendingThumbnail: File? = null
            try {
                if (titleId.isBlank()) {
                    return@withLock failed("missing title id")
                }
                val state = stateFile(titleId, slot)
                state.parentFile?.mkdirs()
                val thumbnail = thumbnailFile(titleId, slot)

                val stagedThumbnail = if (captureThumbnail) {
                    try {
                        File.createTempFile(".preview-", ".jpg", state.parentFile)
                    } catch (cancelled: CancellationException) {
                        throw cancelled
                    } catch (_: Exception) {
                        null
                    }
                } else null
                pendingThumbnail = stagedThumbnail
                val captured = stagedThumbnail != null && try {
                    backend.captureThumbnail(stagedThumbnail.absolutePath) && stagedThumbnail.length() > 0L
                } catch (cancelled: CancellationException) {
                    throw cancelled
                } catch (_: Exception) {
                    false
                }
                val result = backend.save(state.absolutePath, appVersion)
                if (result.isOk) {
                    // Publish the preview in the same repository critical section as the state.
                    val published = captured && stagedThumbnail != null && try {
                        Files.move(stagedThumbnail.toPath(), thumbnail.toPath(),
                            StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
                        true
                    } catch (cancelled: CancellationException) {
                        throw cancelled
                    } catch (_: Exception) {
                        false
                    }
                    if (!published) {
                        thumbnail.delete()
                    }
                }
                result
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (error: Exception) {
                ioError(titleId, error)
            } finally {
                try {
                    pendingThumbnail?.delete()
                } catch (_: Exception) {
                    // Temporary preview cleanup must not replace the save result.
                }
            }
        }.also {
            if (!it.isOk) Log.e("SaveStateRepository", "Save failed for $titleId slot $slot (${it.status}): ${it.error}")
        }

    fun load(titleId: String, slot: Int, allowCrossSession: Boolean = true): SaveStateResult =
        operationLock.withLock {
            try {
                val state = stateFile(titleId, slot)
                if (!state.isFile || state.length() == 0L) {
                    return@withLock failed("slot is empty")
                }
                backend.load(state.absolutePath, allowCrossSession)
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (error: Exception) {
                ioError(titleId, error)
            }
        }.also {
            if (!it.isOk) Log.e("SaveStateRepository", "Load failed for $titleId slot $slot (${it.status}): ${it.error}")
        }

    fun delete(titleId: String, slot: Int): SaveStateResult = operationLock.withLock {
        try {
            val state = stateFile(titleId, slot)
            val result = if (state.isFile) backend.delete(state.absolutePath) else SaveStateResult(
                status = SaveStateResult.STATUS_OK,
                error = "",
                bytes = 0L,
                titleId = titleId,
                appVersion = "",
                engineVersion = 0,
                sessionMatch = true,
                timestamp = 0L
            )
            if (result.isOk) {
                thumbnailFile(titleId, slot).delete()
            }
            result
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (error: Exception) {
            ioError(titleId, error)
        }
    }

    private fun baseName(slot: Int): String = if (slot == QUICK_SLOT) "quick" else "slot_$slot"

    private fun sanitizeTitleId(titleId: String): String =
        titleId.trim().uppercase(Locale.US).replace(Regex("[^A-Z0-9_-]"), "_").ifBlank { "UNKNOWN" }

    private fun failed(message: String): SaveStateResult = SaveStateResult(
        status = SaveStateResult.STATUS_INVALID_FILE,
        error = message,
        bytes = 0L,
        titleId = "",
        appVersion = "",
        engineVersion = 0,
        sessionMatch = true,
        timestamp = 0L
    )

    private fun ioError(titleId: String, error: Exception): SaveStateResult = SaveStateResult(
        status = SaveStateResult.STATUS_IO_ERROR,
        error = error.message ?: "save state operation failed",
        bytes = 0L,
        titleId = titleId,
        appVersion = "",
        engineVersion = 0,
        sessionMatch = true,
        timestamp = 0L
    )
}

fun formatSaveStateTimestamp(timestamp: Long): String {
    if (timestamp <= 0L) return ""
    return SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.getDefault()).format(Date(timestamp))
}

fun formatSaveStateSize(bytes: Long): String {
    if (bytes <= 0L) return "0 B"
    val units = listOf("B", "KB", "MB", "GB")
    var value = bytes.toDouble()
    var index = 0
    while (value >= 1024.0 && index < units.lastIndex) {
        value /= 1024.0
        index++
    }
    return if (index == 0) {
        "${value.toLong()} ${units[index]}"
    } else {
        String.format(Locale.US, "%.1f %s", value, units[index])
    }
}
