package com.sbro.emucorev.data.drive

import android.content.Context
import android.os.SystemClock
import androidx.work.*
import com.sbro.emucorev.core.BackupSessionGate
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.util.concurrent.TimeUnit
import java.util.zip.ZipFile
import kotlin.math.ceil

data class DriveOperation(
    val phase: String = "",
    val percent: Int = -1,
    val transferredBytes: Long = 0,
    val totalBytes: Long = 0,
    val bytesPerSecond: Long = 0,
    val remainingSeconds: Long = -1
)

/** A resumed offset is the baseline, never bytes transferred during this attempt. */
internal class DriveTransferMeter(private val clock: () -> Long = SystemClock::elapsedRealtime) {
    private var lastTime = -1L
    private var lastBytes = 0L
    private var speed = 0.0

    fun sample(phase: String, done: Long, total: Long): DriveOperation? {
        val now = clock()
        val bytes = done.coerceIn(0, total.coerceAtLeast(0))
        if (lastTime >= 0 && bytes >= lastBytes) {
            val elapsed = now - lastTime
            if (elapsed < 500 && (bytes != total || bytes == lastBytes)) return null
            if (elapsed > 0) {
                val measured = (bytes - lastBytes) * 1000.0 / elapsed
                speed = if (speed == 0.0) measured else speed * 0.65 + measured * 0.35
            }
        } else speed = 0.0
        lastTime = now
        lastBytes = bytes
        return DriveOperation(phase = phase,
            percent = if (total > 0) (bytes.toDouble() / total * 100).toInt().coerceIn(0, 100) else -1,
            transferredBytes = bytes, totalBytes = total.coerceAtLeast(0), bytesPerSecond = speed.toLong(),
            remainingSeconds = if (speed > 0) ceil((total - bytes) / speed).toLong().coerceAtLeast(0) else -1)
    }
}

object DriveBackupWork {
    const val TAG = "drive-backup-operation"
    private const val PERIODIC = "drive-backup-periodic"
    internal val mutex = Mutex()
    val operation = MutableStateFlow(DriveOperation())

    fun schedule(context: Context) {
        val settings = DriveBackupState.get(context).value
        val manager = WorkManager.getInstance(context)
        if (!settings.connected || settings.intervalHours == 0) {
            manager.cancelUniqueWork(PERIODIC); return
        }
        val work = PeriodicWorkRequestBuilder<DriveScheduleWorker>(settings.intervalHours.toLong(), TimeUnit.HOURS)
            .setConstraints(constraints(settings)).build()
        manager.enqueueUniquePeriodicWork(PERIODIC, ExistingPeriodicWorkPolicy.UPDATE, work)
    }

    fun afterGame(context: Context) {
        val settings = DriveBackupState.get(context).value
        if (settings.connected && settings.afterGame) enqueue(context, automatic = true)
    }

    /**
     * The backup runs without a foreground service, so the system may stop the worker and leave a
     * pending snapshot behind. Resuming it on the next app start keeps long uploads moving.
     */
    fun resumePending(context: Context) {
        val settings = DriveBackupState.get(context).value
        if (!settings.connected) return
        val archive = DriveBackupArchive(context)
        val pending = File(archive.workDir, "snapshot.zip")
        val pendingInfo = File(archive.workDir, "snapshot.json")
        if (pending.isFile && pendingInfo.isFile) enqueue(context, automatic = true)
    }

    fun enqueue(context: Context, automatic: Boolean = false, restoreId: String? = null,
                categories: Set<String> = DriveBackupArchive.ALL_CATEGORIES) {
        val settings = DriveBackupState.get(context).value
        if (!settings.connected) return
        val work = OneTimeWorkRequestBuilder<DriveBackupWorker>()
            .addTag(TAG).setConstraints(constraints(settings))
            .setBackoffCriteria(BackoffPolicy.EXPONENTIAL, 30, TimeUnit.SECONDS)
            .setInputData(workDataOf("email" to settings.email, "automatic" to automatic,
                "restoreId" to restoreId, "categories" to categories.toTypedArray())).build()
        WorkManager.getInstance(context).enqueueUniqueWork(TAG,
            if (automatic) ExistingWorkPolicy.APPEND_OR_REPLACE else ExistingWorkPolicy.KEEP, work)
    }

    fun cancel(context: Context) { WorkManager.getInstance(context).cancelUniqueWork(TAG) }
    private fun constraints(settings: DriveBackupSettings) = Constraints.Builder()
        .setRequiredNetworkType(if (settings.wifiOnly) NetworkType.UNMETERED else NetworkType.CONNECTED)
        .setRequiresCharging(settings.chargingOnly).setRequiresBatteryNotLow(true)
        .setRequiresStorageNotLow(true).build()
}

class DriveScheduleWorker(context: Context, params: WorkerParameters) : CoroutineWorker(context, params) {
    override suspend fun doWork(): Result {
        DriveBackupWork.enqueue(applicationContext, automatic = true)
        return Result.success()
    }
}

class DriveBackupWorker(context: Context, params: WorkerParameters) : CoroutineWorker(context, params) {
    private val store = DriveBackupState.get(context)
    private var transferMeter = DriveTransferMeter()
    override suspend fun doWork(): Result = withContext(Dispatchers.IO) {
        DriveBackupWork.mutex.withLock {
            val settings = store.value
            val email = inputData.getString("email")
            if (!settings.connected || settings.email != email) return@withLock Result.success()
            try {
                BackupSessionGate.checkpoint()
                store.update { it.copy(lastError = "") }
                val archive = DriveBackupArchive(applicationContext)
                val api = DriveBackupApi(applicationContext, settings.email)
                val restoreId = inputData.getString("restoreId")
                if (restoreId != null) {
                    val entry = api.list().firstOrNull { it.id == restoreId } ?: throw DriveBackupException("missing")
                    phase("download")
                    val download = File(archive.workDir, "download.zip")
                    try {
                        api.download(entry, download) { done, total -> progress("download", done, total) }
                        phase("verify")
                        val manifest = archive.extract(download)
                        phase("restore")
                        archive.restore(manifest, inputData.getStringArray("categories")?.toSet().orEmpty())
                        store.update { it.copy(lastDigest = "", lastError = "restored") }
                    } finally {
                        download.delete()
                        DriveBackupArchive.clearChild(archive.workDir, File(archive.workDir, "extracted"))
                    }
                } else {
                    phase("prepare")
                    val pending = File(archive.workDir, "snapshot.zip")
                    val pendingInfo = File(archive.workDir, "snapshot.json")
                    val info = runCatching { JSONObject(pendingInfo.readText()) }.getOrNull()
                    val resumed = pending.isFile && info?.optString("email") == email
                    val snapshot = if (resumed) {
                        val manifest = ZipFile(pending).use { zip ->
                            JSONObject(zip.getInputStream(zip.getEntry("manifest.json")).bufferedReader().use { it.readText() })
                        }
                        DriveSnapshot(pending, requireNotNull(info).getString("digest"), manifest)
                    } else {
                        val fresh = archive.create(settings.includeSaveData)
                        if (inputData.getBoolean("automatic", false) && fresh.digest == settings.lastDigest &&
                            api.list().any { it.id == settings.lastFileId }) {
                            fresh.file.delete(); return@withLock Result.success()
                        }
                        if (pending.exists() && !pending.delete()) throw DriveBackupException("storage")
                        if (!fresh.file.renameTo(pending)) throw DriveBackupException("storage")
                        DriveBackupArchive.writeAtomic(pendingInfo, JSONObject().put("email", email).put("digest", fresh.digest))
                        fresh.copy(file = pending)
                    }
                    phase("upload")
                    val id = api.upload(snapshot, verifying = { phase("verify") }) { done, total -> progress("upload", done, total) }
                    store.update { it.copy(lastBackupMs = snapshot.manifest.getLong("createdAt"),
                        lastSize = snapshot.file.length(), lastDigest = snapshot.digest, lastFileId = id,
                        needsAuthorization = false, lastError = "") }
                    pendingInfo.delete(); pending.delete()
                    // Retention is per source device, and only after a verified, published replacement.
                    val own = api.list().filter { it.deviceId == settings.deviceId }
                    own.filter { it.id != id }.drop((settings.keepCopies - 1).coerceAtLeast(0)).forEach { api.trash(it.id) }
                    // A resumed immutable copy may predate another game session; follow it with a
                    // fresh change check so "Back up now" ultimately covers current data too.
                    if (resumed) DriveBackupWork.enqueue(applicationContext, automatic = true)
                }
                Result.success()
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (error: Exception) {
                val reason = when (error) {
                    is DriveBackupException -> error.reason
                    is IOException -> "network"
                    else -> "failed"
                }
                store.update { it.copy(lastError = reason, needsAuthorization = reason == "auth") }
                if (reason in setOf("network", "busy") && runAttemptCount < 8) Result.retry() else Result.failure()
            } finally { DriveBackupWork.operation.value = DriveOperation() }
        }
    }

    private suspend fun phase(value: String) {
        transferMeter = DriveTransferMeter()
        DriveBackupWork.operation.value = DriveOperation(value)
        setProgress(workDataOf("phase" to value))
    }

    private suspend fun progress(phase: String, done: Long, total: Long) {
        val value = transferMeter.sample(phase, done, total) ?: return
        DriveBackupWork.operation.value = value
        setProgress(workDataOf("phase" to phase, "percent" to value.percent,
            "transferredBytes" to value.transferredBytes, "totalBytes" to value.totalBytes,
            "bytesPerSecond" to value.bytesPerSecond, "remainingSeconds" to value.remainingSeconds))
    }
}
