package com.sbro.emucorev.data

import android.content.Context
import android.util.Base64
import com.google.android.gms.tasks.Task
import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.firestore.CollectionReference
import com.google.firebase.firestore.DocumentSnapshot
import com.google.firebase.firestore.FieldValue
import com.google.firebase.firestore.FirebaseFirestore
import com.google.firebase.firestore.SetOptions
import com.sbro.emucorev.BuildConfig
import com.sbro.emucorev.core.BackupSessionGate
import com.sbro.emucorev.core.EmulatorStorage
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import java.io.File
import java.security.MessageDigest
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

data class TrophyCloudSyncResult(val backedUp: Int = 0, val restored: Int = 0)

/**
 * Keeps Vita trophy progress (config files and TROPUSR.DAT) on the signed-in account so a
 * reinstall or a second device can recover unlocked trophies after the game is installed again.
 */
class TrophyCloudRepository(context: Context) {
    private val appContext = context.applicationContext
    private val auth = FirebaseAuth.getInstance()
    private val firestore = FirebaseFirestore.getInstance()
    private val preferences = appContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    suspend fun sync(): TrophyCloudSyncResult {
        val uid = auth.currentUser?.uid ?: return TrophyCloudSyncResult()
        return runCatching { BackupSessionGate.whileStopped { syncLocked(uid) } }
            .getOrDefault(TrophyCloudSyncResult())
    }

    private suspend fun syncLocked(uid: String): TrophyCloudSyncResult {
        val collection = firestore.collection(USERS).document(uid).collection(TROPHY_BACKUPS)
        var restored = 0
        collection.get().await().documents.forEach { document ->
            val commId = document.id
            if (!isSafeCommunicationId(commId)) return@forEach
            // Local progress always wins; the account copy only fills in missing trophies.
            if (localProgressFile(commId) != null) return@forEach
            if (restoreDocument(commId, document)) restored++
        }

        var backedUp = 0
        collectLocalFiles().forEach { (commId, files) ->
            val fingerprint = fingerprint(files)
            if (preferences.getString(preferenceKey(commId), null) == fingerprint) return@forEach
            if (upload(collection, commId, files)) {
                preferences.edit().putString(preferenceKey(commId), fingerprint).apply()
                backedUp++
            }
        }
        return TrophyCloudSyncResult(backedUp = backedUp, restored = restored)
    }

    private fun collectLocalFiles(): Map<String, Map<String, File>> {
        val result = linkedMapOf<String, MutableMap<String, File>>()
        confRoots().forEach { root ->
            root.listFiles().orEmpty().filter(File::isDirectory).forEach { directory ->
                val commId = directory.name
                if (!isSafeCommunicationId(commId)) return@forEach
                val bucket = result.getOrPut(commId) { linkedMapOf() }
                directory.listFiles().orEmpty()
                    .filter { it.isFile && isSafeFileName(it.name) && it.length() in 1..MAX_FILE_BYTES }
                    .forEach { file -> bucket[file.name] = file }
            }
        }
        dataRoots().forEach { root ->
            root.listFiles().orEmpty().filter(File::isDirectory).forEach { directory ->
                val commId = directory.name
                if (!isSafeCommunicationId(commId)) return@forEach
                val progress = File(directory, PROGRESS_NAME)
                if (progress.isFile && progress.length() in 1..MAX_FILE_BYTES) {
                    result.getOrPut(commId) { linkedMapOf() }[PROGRESS_NAME] = progress
                }
            }
        }
        return result.filterValues { it.isNotEmpty() }
    }

    private suspend fun upload(
        collection: CollectionReference,
        commId: String,
        files: Map<String, File>
    ): Boolean {
        val payload = mutableListOf<Map<String, String>>()
        var totalChars = 0
        files.forEach { (name, file) ->
            val bytes = runCatching { file.readBytes() }.getOrNull() ?: return@forEach
            if (bytes.isEmpty() || bytes.size > MAX_FILE_BYTES) return@forEach
            val encoded = Base64.encodeToString(bytes, Base64.NO_WRAP)
            totalChars += encoded.length
            if (totalChars > MAX_PAYLOAD_CHARS) return false
            payload += mapOf("name" to name, "data" to encoded)
        }
        if (payload.isEmpty()) return false
        val document = collection.document(commId)
        val existing = document.get().await()
        val data = mutableMapOf<String, Any>(
            "communicationId" to commId,
            "schemaVersion" to 1,
            "appVersion" to BuildConfig.VERSION_NAME,
            "files" to payload,
            FIELD_UPDATED_AT to FieldValue.serverTimestamp()
        )
        if (!existing.exists()) data[FIELD_CREATED_AT] = FieldValue.serverTimestamp()
        document.set(data, SetOptions.merge()).await()
        return true
    }

    private fun restoreDocument(commId: String, document: DocumentSnapshot): Boolean {
        val files = document.get("files") as? List<*> ?: return false
        val userId = EmulatorStorage.activeUserId(appContext)
        val userRoot = File(EmulatorStorage.vitaRoot(appContext), "ux0/user/$userId/trophy")
        val confDir = File(userRoot, "conf/$commId")
        val dataDir = File(userRoot, "data/$commId")
        var written = false
        files.forEach { raw ->
            val entry = raw as? Map<*, *> ?: return@forEach
            val name = entry["name"] as? String ?: return@forEach
            val encoded = entry["data"] as? String ?: return@forEach
            if (!isSafeFileName(name)) return@forEach
            val bytes = runCatching { Base64.decode(encoded, Base64.DEFAULT) }.getOrNull() ?: return@forEach
            if (bytes.isEmpty() || bytes.size > MAX_FILE_BYTES) return@forEach
            val targetDir = if (name.equals(PROGRESS_NAME, ignoreCase = true)) dataDir else confDir
            runCatching {
                targetDir.mkdirs()
                File(targetDir, name).writeBytes(bytes)
            }.onSuccess { written = true }
        }
        return written
    }

    private fun confRoots(): List<File> {
        val userRoot = File(EmulatorStorage.vitaRoot(appContext), "ux0/user")
        val roots = mutableListOf<File>()
        userRoot.listFiles().orEmpty()
            .filter(File::isDirectory)
            .mapTo(roots) { File(it, "trophy/conf") }
        roots += File(userRoot, "trophy/conf")
        return roots.filter { it.isDirectory }.distinctBy { it.absolutePath }
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

    private fun localProgressFile(commId: String): File? {
        return dataRoots()
            .map { File(File(it, commId), PROGRESS_NAME) }
            .firstOrNull { it.isFile }
    }

    private fun fingerprint(files: Map<String, File>): String {
        val digest = MessageDigest.getInstance("SHA-256")
        files.toSortedMap().forEach { (name, file) ->
            digest.update("$name:${file.length()}:${file.lastModified()}\n".toByteArray(Charsets.UTF_8))
        }
        return digest.digest().joinToString("") { "%02x".format(it) }
    }

    private fun preferenceKey(commId: String) = "backup_$commId"

    private suspend fun <T> Task<T>.await(): T = suspendCancellableCoroutine { continuation ->
        addOnSuccessListener { result -> continuation.resume(result) }
        addOnFailureListener { error -> continuation.resumeWithException(error) }
        addOnCanceledListener { continuation.cancel() }
    }

    companion object {
        private const val USERS = "users"
        private const val TROPHY_BACKUPS = "trophyBackups"
        private const val PROGRESS_NAME = "TROPUSR.DAT"
        private const val FIELD_CREATED_AT = "createdAt"
        private const val FIELD_UPDATED_AT = "updatedAt"
        private const val PREFS = "trophy_cloud_backup"
        private const val MAX_FILE_BYTES = 512 * 1024
        private const val MAX_PAYLOAD_CHARS = 900_000

        private fun isSafeCommunicationId(value: String) = value.matches(Regex("[A-Za-z0-9_]{1,40}"))

        private fun isSafeFileName(value: String) =
            value.matches(Regex("[A-Za-z0-9_.-]{1,64}")) && !value.startsWith(".")

        private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

        fun syncAsync(context: Context) {
            val applicationContext = context.applicationContext
            scope.launch { runCatching { TrophyCloudRepository(applicationContext).sync() } }
        }
    }
}
