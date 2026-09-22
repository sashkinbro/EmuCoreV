package com.sbro.emucorev.core

import androidx.annotation.Keep
import org.json.JSONObject

data class SaveStateResult(
    val status: String,
    val error: String,
    val bytes: Long,
    val titleId: String,
    val appVersion: String,
    val engineVersion: Int,
    val sessionMatch: Boolean,
    val timestamp: Long
) {
    val isOk: Boolean get() = status == STATUS_OK
    val isSessionMismatch: Boolean get() = status == STATUS_SESSION_MISMATCH
    val isTitleMismatch: Boolean get() = status == STATUS_TITLE_MISMATCH

    companion object {
        const val STATUS_OK = "ok"
        const val STATUS_NOT_RUNNING = "not-running"
        const val STATUS_INVALID_FILE = "invalid-file"
        const val STATUS_UNSUPPORTED_VERSION = "unsupported-version"
        const val STATUS_TITLE_MISMATCH = "title-mismatch"
        const val STATUS_SESSION_MISMATCH = "session-mismatch"
        const val STATUS_NO_SPACE = "no-space"
        const val STATUS_IO_ERROR = "io-error"
        const val STATUS_INTERNAL_ERROR = "internal-error"
    }
}

@Keep
object SaveStateBridge {
    @Keep
    external fun nativeSaveState(path: String, appVersion: String): String

    @Keep
    external fun nativeLoadState(path: String, allowCrossSession: Boolean): String

    @Keep
    external fun nativeInspectSaveState(path: String): String

    @Keep
    external fun nativeDeleteSaveState(path: String): String

    @Keep
    external fun nativeCaptureThumbnail(path: String, maxWidth: Int): Boolean

    @Keep
    external fun nativeGetRunningTitleId(): String

    fun runningTitleId(): String =
        runCatching { nativeGetRunningTitleId() }.getOrDefault("")

    fun save(path: String, appVersion: String): SaveStateResult =
        parse(runCatching { nativeSaveState(path, appVersion) }.getOrNull())

    fun load(path: String, allowCrossSession: Boolean = false): SaveStateResult =
        parse(runCatching { nativeLoadState(path, allowCrossSession) }.getOrNull())

    fun inspect(path: String): SaveStateResult =
        parse(runCatching { nativeInspectSaveState(path) }.getOrNull())

    fun delete(path: String): SaveStateResult =
        parse(runCatching { nativeDeleteSaveState(path) }.getOrNull())

    fun captureThumbnail(path: String, maxWidth: Int = 384): Boolean =
        runCatching { nativeCaptureThumbnail(path, maxWidth) }.getOrDefault(false)

    private fun parse(raw: String?): SaveStateResult {
        if (raw.isNullOrBlank()) {
            return SaveStateResult(
                status = SaveStateResult.STATUS_INTERNAL_ERROR,
                error = "empty native response",
                bytes = 0L,
                titleId = "",
                appVersion = "",
                engineVersion = 0,
                sessionMatch = true,
                timestamp = 0L
            )
        }
        return runCatching {
            val json = JSONObject(raw)
            SaveStateResult(
                status = json.optString("status", SaveStateResult.STATUS_INTERNAL_ERROR),
                error = json.optString("error", ""),
                bytes = json.optLong("bytes", 0L),
                titleId = json.optString("title", ""),
                appVersion = json.optString("appVersion", ""),
                engineVersion = json.optInt("engineVersion", 0),
                sessionMatch = json.optBoolean("sessionMatch", true),
                timestamp = json.optLong("timestamp", 0L)
            )
        }.getOrElse { error ->
            SaveStateResult(
                status = SaveStateResult.STATUS_INTERNAL_ERROR,
                error = error.message ?: "invalid native response",
                bytes = 0L,
                titleId = "",
                appVersion = "",
                engineVersion = 0,
                sessionMatch = true,
                timestamp = 0L
            )
        }
    }
}
