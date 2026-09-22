package com.sbro.emucorev.core

import android.content.Context
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

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

class SaveStateRepository(private val context: Context) {

    companion object {
        const val QUICK_SLOT = 0
        const val FIRST_SLOT = 1
        const val LAST_SLOT = 5
        val SLOT_ORDER = listOf(QUICK_SLOT, 1, 2, 3, 4, 5)

        private const val DIRECTORY = "savestates"
        private const val STATE_EXTENSION = ".ecvs"
        private const val THUMBNAIL_EXTENSION = ".jpg"
    }

    fun slotsDirectory(titleId: String): File =
        File(EmulatorStorage.runtimeRoot(context), "$DIRECTORY/${sanitizeTitleId(titleId)}").apply { mkdirs() }

    fun gamesWithSlots(): List<String> {
        val root = File(EmulatorStorage.runtimeRoot(context), DIRECTORY)
        if (!root.isDirectory) return emptyList()
        return root.listFiles().orEmpty()
            .filter { it.isDirectory }
            .map { it.name }
            .sorted()
    }

    fun stateFile(titleId: String, slot: Int): File =
        File(slotsDirectory(titleId), baseName(slot) + STATE_EXTENSION)

    fun thumbnailFile(titleId: String, slot: Int): File =
        File(slotsDirectory(titleId), baseName(slot) + THUMBNAIL_EXTENSION)

    fun listSlots(titleId: String): List<SaveStateSlot> {
        if (titleId.isBlank()) return emptyList()
        return SLOT_ORDER.map { slot ->
            val state = stateFile(titleId, slot)
            val thumbnail = thumbnailFile(titleId, slot)
            val exists = state.isFile && state.length() > 0
            val inspection = if (exists) SaveStateBridge.inspect(state.absolutePath) else null
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

    fun save(titleId: String, slot: Int, appVersion: String, captureThumbnail: Boolean = true): SaveStateResult {
        if (titleId.isBlank()) {
            return failed("missing title id")
        }
        val state = stateFile(titleId, slot)
        state.parentFile?.mkdirs()
        val thumbnail = thumbnailFile(titleId, slot)

        if (captureThumbnail) {
            val captured = SaveStateBridge.captureThumbnail(thumbnail.absolutePath)
            if (!captured) {
                thumbnail.delete()
            }
        }

        val result = SaveStateBridge.save(state.absolutePath, appVersion)
        if (!result.isOk) {
            state.delete()
            thumbnail.delete()
        }
        return result
    }

    fun load(titleId: String, slot: Int, allowCrossSession: Boolean = true): SaveStateResult {
        val state = stateFile(titleId, slot)
        if (!state.isFile || state.length() == 0L) {
            return failed("slot is empty")
        }
        return SaveStateBridge.load(state.absolutePath, allowCrossSession)
    }

    fun delete(titleId: String, slot: Int): SaveStateResult {
        val state = stateFile(titleId, slot)
        thumbnailFile(titleId, slot).delete()
        val result = if (state.isFile) SaveStateBridge.delete(state.absolutePath) else SaveStateResult(
            status = SaveStateResult.STATUS_OK,
            error = "",
            bytes = 0L,
            titleId = titleId,
            appVersion = "",
            engineVersion = 0,
            sessionMatch = true,
            timestamp = 0L
        )
        if (!result.isOk) {
            state.delete()
        }
        return result
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
