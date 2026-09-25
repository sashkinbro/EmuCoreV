package com.sbro.emucorev.data.drive

import android.content.Context
import android.util.AtomicFile
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.json.JSONObject
import java.io.File
import java.util.UUID

data class DriveBackupSettings(
    val email: String = "",
    val displayName: String = "",
    val photoUrl: String = "",
    val deviceId: String = UUID.randomUUID().toString(),
    val intervalHours: Int = 0,
    val afterGame: Boolean = false,
    val wifiOnly: Boolean = true,
    val chargingOnly: Boolean = false,
    val includeSaveData: Boolean = true,
    val keepCopies: Int = 5,
    val lastBackupMs: Long = 0,
    val lastSize: Long = 0,
    val lastDigest: String = "",
    val lastFileId: String = "",
    val needsAuthorization: Boolean = false,
    val lastError: String = ""
) {
    val connected get() = email.isNotBlank()
}

/** Device-local only: neither Android backup nor the Drive archive contains account/task state. */
class DriveBackupState private constructor(context: Context) {
    private val file = AtomicFile(File(context.noBackupFilesDir, "drive-backup-state.json"))
    private val mutable = MutableStateFlow(read())
    val flow = mutable.asStateFlow()
    val value get() = mutable.value

    @Synchronized
    fun update(transform: (DriveBackupSettings) -> DriveBackupSettings) {
        val next = transform(value)
        val json = JSONObject().apply {
            put("email", next.email); put("deviceId", next.deviceId)
            put("displayName", next.displayName); put("photoUrl", next.photoUrl)
            put("intervalHours", next.intervalHours); put("afterGame", next.afterGame)
            put("wifiOnly", next.wifiOnly); put("chargingOnly", next.chargingOnly)
            put("includeSaveData", next.includeSaveData)
            put("keepCopies", next.keepCopies); put("lastBackupMs", next.lastBackupMs)
            put("lastSize", next.lastSize); put("lastDigest", next.lastDigest)
            put("lastFileId", next.lastFileId); put("needsAuthorization", next.needsAuthorization)
            put("lastError", next.lastError)
        }
        val output = file.startWrite()
        try {
            output.write(json.toString().toByteArray(Charsets.UTF_8))
            file.finishWrite(output)
        } catch (error: Throwable) {
            file.failWrite(output)
            throw error
        }
        mutable.value = next
    }

    private fun read(): DriveBackupSettings = runCatching {
        val json = JSONObject(file.openRead().bufferedReader().use { it.readText() })
        DriveBackupSettings(
            email = json.optString("email"),
            displayName = json.optString("displayName"), photoUrl = json.optString("photoUrl"),
            deviceId = json.optString("deviceId").ifBlank { UUID.randomUUID().toString() },
            intervalHours = json.optInt("intervalHours").takeIf { it in listOf(0, 6, 12, 24) } ?: 0,
            afterGame = json.optBoolean("afterGame"), wifiOnly = json.optBoolean("wifiOnly", true),
            chargingOnly = json.optBoolean("chargingOnly"),
            includeSaveData = json.optBoolean("includeSaveData", true),
            keepCopies = json.optInt("keepCopies", 5).coerceIn(1, 20),
            lastBackupMs = json.optLong("lastBackupMs"), lastSize = json.optLong("lastSize"),
            lastDigest = json.optString("lastDigest"), lastFileId = json.optString("lastFileId"),
            needsAuthorization = json.optBoolean("needsAuthorization"), lastError = json.optString("lastError")
        )
    }.getOrDefault(DriveBackupSettings())

    companion object {
        @Volatile private var instance: DriveBackupState? = null
        fun get(context: Context): DriveBackupState = instance ?: synchronized(this) {
            instance ?: DriveBackupState(context.applicationContext).also { instance = it }
        }
    }
}

class DriveBackupException(val reason: String, cause: Throwable? = null) : Exception(reason, cause)
