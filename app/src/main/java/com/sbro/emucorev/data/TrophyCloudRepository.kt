package com.sbro.emucorev.data

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.util.Base64
import com.google.android.gms.tasks.Task
import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.firestore.CollectionReference
import com.google.firebase.firestore.DocumentSnapshot
import com.google.firebase.firestore.FieldValue
import com.google.firebase.firestore.FirebaseFirestore
import com.google.firebase.firestore.SetOptions
import com.sbro.emucorev.BuildConfig
import com.sbro.emucorev.core.EmulatorStorage
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull
import java.io.File
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

data class TrophyCloudSyncResult(
    val backedUp: Int = 0,
    val restored: Int = 0,
    val pending: Boolean = false
)

/**
 * Keeps every game's TROPUSR.DAT in one compressed account archive so a reinstall or a second
 * device can recover unlocked trophies. Unlocks are merged, never reset by an older device.
 */
class TrophyCloudRepository(context: Context) {
    private val appContext = context.applicationContext
    private val auth = FirebaseAuth.getInstance()
    private val firestore = FirebaseFirestore.getInstance()
    private val preferences = appContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    suspend fun sync(timeoutMs: Long = SYNC_TIMEOUT_MS): TrophyCloudSyncResult {
        val uid = auth.currentUser?.uid
        if (uid.isNullOrBlank()) {
            clearPending()
            return TrophyCloudSyncResult()
        }
        if (!hasNetwork() || TrophySyncSessionGate.shared.isBusy) return pendingResult()
        val result = try {
            withTimeoutOrNull(timeoutMs) {
                TrophySyncSessionGate.shared.whileStopped { syncLocked(uid) }
            } ?: TrophyCloudSyncResult(pending = true)
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: Exception) {
            TrophyCloudSyncResult(pending = true)
        }
        return if (result.pending) pendingResult() else result.also { clearPending() }
    }

    private suspend fun syncLocked(uid: String): TrophyCloudSyncResult {
        val collection = firestore.collection(USERS).document(uid).collection(TROPHY_BACKUPS)
        val document = collection.document(ARCHIVE_ID).get().await()
        val remoteArchive = document.takeIf { it.exists() }?.let(::archiveBytes)
        val remote = remoteArchive
            ?.let { runCatching { TrophyProgressArchive.decode(it) }.getOrNull() }
            ?: legacyProgress(collection)
        val local = localProgress()
        val merged = TrophyProgressArchive.merge(local.mapValues { (_, value) -> value.bytes }, remote)
        val restored = writeMergedProgress(merged, local)
        val archive = if (merged.isEmpty()) null else TrophyProgressArchive.encode(merged)
        val backedUp = archive != null &&
            (remoteArchive == null || !remoteArchive.contentEquals(archive)) &&
            upload(collection, document, archive)
        return TrophyCloudSyncResult(backedUp = if (backedUp) 1 else 0, restored = restored)
    }

    private fun archiveBytes(document: DocumentSnapshot): ByteArray? {
        val files = document.get("files") as? List<*> ?: return null
        files.forEach { raw ->
            val entry = raw as? Map<*, *> ?: return@forEach
            if (entry["name"] != ARCHIVE_FILE_NAME) return@forEach
            val encoded = entry["data"] as? String ?: return@forEach
            if (encoded.length !in 1..MAX_PAYLOAD_CHARS) return@forEach
            val bytes = runCatching { Base64.decode(encoded, Base64.DEFAULT) }.getOrNull()
                ?: return@forEach
            if (bytes.size in 22..TrophyProgressArchive.MAX_ARCHIVE_BYTES) return bytes
        }
        return null
    }

    private suspend fun legacyProgress(collection: CollectionReference): Map<String, ByteArray> {
        val result = linkedMapOf<String, ByteArray>()
        collection.get().await().documents.forEach { document ->
            val commId = document.id
            if (commId == ARCHIVE_ID || !TrophyProgressArchive.safeId(commId)) return@forEach
            val files = document.get("files") as? List<*> ?: return@forEach
            files.forEach { raw ->
                val entry = raw as? Map<*, *> ?: return@forEach
                if (!(entry["name"] as? String).equals(TrophyProgressArchive.PROGRESS_NAME, ignoreCase = true)) {
                    return@forEach
                }
                val encoded = entry["data"] as? String ?: return@forEach
                if (encoded.length !in 1..MAX_PAYLOAD_CHARS) return@forEach
                val bytes = runCatching { Base64.decode(encoded, Base64.DEFAULT) }.getOrNull()
                    ?: return@forEach
                if (TrophyProgressArchive.validProgress(bytes)) result[commId] = bytes
            }
        }
        return result
    }

    private fun localProgress(): Map<String, LocalProgress> {
        val result = linkedMapOf<String, LocalProgress>()
        dataRoots().forEach { root ->
            root.listFiles().orEmpty().filter(File::isDirectory).forEach { directory ->
                val commId = directory.name
                if (result.containsKey(commId) || !TrophyProgressArchive.safeId(commId)) return@forEach
                val file = File(directory, TrophyProgressArchive.PROGRESS_NAME)
                if (!file.isFile) return@forEach
                val bytes = runCatching { file.readBytes() }.getOrNull() ?: return@forEach
                if (TrophyProgressArchive.validProgress(bytes)) {
                    result[commId] = LocalProgress(file, bytes)
                }
            }
        }
        return result
    }

    private fun writeMergedProgress(
        merged: Map<String, ByteArray>,
        local: Map<String, LocalProgress>
    ): Int {
        var written = 0
        merged.forEach { (commId, bytes) ->
            val previous = local[commId]
            if (previous != null && previous.bytes.contentEquals(bytes)) return@forEach
            val target = previous?.file ?: File(File(activeDataRoot(), commId),
                TrophyProgressArchive.PROGRESS_NAME)
            if (writeProgress(target, bytes)) written++
        }
        return written
    }

    private fun writeProgress(file: File, bytes: ByteArray): Boolean {
        if (!TrophyProgressArchive.validProgress(bytes)) return false
        val directory = file.parentFile ?: return false
        return runCatching {
            directory.mkdirs()
            val temporary = File(directory, "${file.name}.tmp")
            try {
                temporary.writeBytes(bytes)
                try {
                    Files.move(temporary.toPath(), file.toPath(),
                        StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE)
                } catch (_: Exception) {
                    Files.move(temporary.toPath(), file.toPath(),
                        StandardCopyOption.REPLACE_EXISTING)
                }
            } finally {
                temporary.delete()
            }
            true
        }.getOrDefault(false)
    }

    private suspend fun upload(
        collection: CollectionReference,
        existing: DocumentSnapshot,
        archive: ByteArray
    ): Boolean {
        val encoded = Base64.encodeToString(archive, Base64.NO_WRAP)
        if (encoded.length > MAX_PAYLOAD_CHARS) return false
        val data = mutableMapOf<String, Any>(
            "communicationId" to ARCHIVE_ID,
            "schemaVersion" to ARCHIVE_SCHEMA_VERSION,
            "appVersion" to BuildConfig.VERSION_NAME,
            "files" to listOf(mapOf("name" to ARCHIVE_FILE_NAME, "data" to encoded)),
            FIELD_UPDATED_AT to FieldValue.serverTimestamp()
        )
        if (!existing.exists()) data[FIELD_CREATED_AT] = FieldValue.serverTimestamp()
        collection.document(ARCHIVE_ID).set(data, SetOptions.merge()).await()
        return true
    }

    private fun dataRoots(): List<File> {
        val userRoot = File(EmulatorStorage.vitaRoot(appContext), "ux0/user")
        val roots = mutableListOf<File>()
        userRoot.listFiles().orEmpty()
            .filter(File::isDirectory)
            .mapTo(roots) { File(it, "trophy/data") }
        roots += File(userRoot, "trophy/data")
        return roots.filter { it.isDirectory }.distinctBy { it.absolutePath }
    }

    private fun activeDataRoot(): File {
        val userId = EmulatorStorage.activeUserId(appContext)
        return File(EmulatorStorage.vitaRoot(appContext), "ux0/user/$userId/trophy/data")
    }

    private fun hasNetwork(): Boolean {
        val manager = appContext.getSystemService(ConnectivityManager::class.java) ?: return false
        val network = manager.activeNetwork ?: return false
        val capabilities = manager.getNetworkCapabilities(network) ?: return false
        return capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
    }

    private fun pendingResult(): TrophyCloudSyncResult {
        preferences.edit().putBoolean(KEY_PENDING, true).apply()
        TrophySyncScheduler.schedule(appContext)
        return TrophyCloudSyncResult(pending = true)
    }

    private fun clearPending() {
        preferences.edit().remove(KEY_PENDING).apply()
    }

    private suspend fun <T> Task<T>.await(): T = suspendCancellableCoroutine { continuation ->
        addOnSuccessListener { result -> continuation.resume(result) }
        addOnFailureListener { error -> continuation.resumeWithException(error) }
        addOnCanceledListener { continuation.cancel() }
    }

    private data class LocalProgress(val file: File, val bytes: ByteArray)

    companion object {
        private const val USERS = "users"
        private const val TROPHY_BACKUPS = "trophyBackups"
        private const val ARCHIVE_ID = "archive"
        private const val ARCHIVE_FILE_NAME = "trophy-progress.zip"
        private const val ARCHIVE_SCHEMA_VERSION = 2
        private const val FIELD_CREATED_AT = "createdAt"
        private const val FIELD_UPDATED_AT = "updatedAt"
        private const val PREFS = "trophy_cloud_backup"
        private const val KEY_PENDING = "pending"
        private const val MAX_PAYLOAD_CHARS = 900_000
        private const val SYNC_TIMEOUT_MS = 15_000L

        fun resumePending(context: Context) {
            val appContext = context.applicationContext
            val pending = appContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .getBoolean(KEY_PENDING, false)
            if (pending) TrophySyncScheduler.schedule(appContext)
        }
    }
}
