package com.sbro.emucorev.data.drive

import android.content.Context
import android.os.Build
import android.util.AtomicFile
import com.sbro.emucorev.BuildConfig
import com.sbro.emucorev.core.BackupSessionGate
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.core.SettingsBackupRepository
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.CustomizationPreferences
import com.sbro.emucorev.data.ProfileDeviceInfoProvider
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.InputStream
import java.io.OutputStream
import java.io.IOException
import kotlinx.coroutines.CancellationException
import org.json.JSONException
import java.util.zip.ZipException
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.security.MessageDigest
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipOutputStream

data class DriveSnapshot(val file: File, val digest: String, val manifest: JSONObject)

/** An explicit data allowlist; never archives filesDir, databases, credentials, games or BIOS. */
class DriveBackupArchive(private val context: Context) {
    private val preferences = AppPreferences(context)
    private val settingsRepository = SettingsBackupRepository(
        context,
        preferences,
        VitaCoreConfigRepository(context),
        CustomizationPreferences(context)
    )
    val workDir = File(context.noBackupFilesDir, "drive-backup").apply { mkdirs() }
    private val transaction = File(context.noBackupFilesDir, "drive-restore")
    private val journal get() = AtomicFile(File(transaction, "journal.json"))

    private fun roots(): Map<String, File> {
        val storage = EmulatorStorage.storageRoot(context)
        if (!storage.isDirectory || !storage.canRead() || !storage.canWrite()) {
            throw DriveBackupException("storage")
        }
        return linkedMapOf(
            "customization" to File(context.filesDir, "customization"),
            "per-game" to File(EmulatorStorage.runtimeRoot(context), "config"),
            "cheats" to File(EmulatorStorage.storageRoot(context), "cheats"),
            "save-data" to EmulatorStorage.ux0UserRoot(context),
            "trophies" to EmulatorStorage.ux0UserRoot(context)
        )
    }

    private fun allowed(name: String): Boolean {
        if (name in JSON_FILES) return true
        val parts = name.split('/')
        if (parts.size < 2 || parts.any { it.isBlank() || it == "." || it == ".." || '\\' in it || ':' in it }) return false
        return when (parts[0]) {
            "customization" -> parts.size == 2 && (
                parts[1].startsWith("background.") && parts[1].substringAfterLast('.').lowercase() in setOf("jpg", "jpeg", "png", "gif", "webp", "bmp", "mp4", "webm")
                || parts[1].startsWith("font.") && parts[1].substringAfterLast('.').lowercase() in setOf("ttf", "otf")
                )
            "per-game" -> parts.size == 2 && parts[1].matches(Regex("config_[A-Za-z0-9_\\-]{1,40}\\.xml"))
            "cheats" -> parts.size == 2 && parts[1].endsWith(".psv", true)
            "save-data" -> parts.size >= 3 && parts[0].matches(Regex("[A-Za-z0-9]{1,16}")) && parts[1] == "savedata"
            "trophies" -> parts.size >= 3 && parts[0].matches(Regex("[A-Za-z0-9]{1,16}")) && parts[1] == "trophy"
            else -> false
        }
    }

    suspend fun create(includeSaveData: Boolean = true): DriveSnapshot = BackupSessionGate.whileStopped {
        recoverPending()
        val file = File(workDir, "snapshot-new.zip")
        val entries = JSONArray()
        val contentHash = MessageDigest.getInstance("SHA-256")
        val sources = sortedMapOf<String, File>()
        roots().forEach { (prefix, root) ->
            if ((prefix == "save-data" || prefix == "trophies") && !includeSaveData) return@forEach
            if (root.isDirectory) root.walkTopDown().onEnter { !Files.isSymbolicLink(it.toPath()) }.forEach { source ->
                if (source.isFile && !Files.isSymbolicLink(source.toPath())) {
                    val name = "$prefix/${source.relativeTo(root).invariantSeparatorsPath}"
                    if (allowed(name) && source.canonicalPath.startsWith(root.canonicalPath + File.separator)) sources[name] = source
                }
            }
        }
        val json = sortedMapOf(
            "settings.json" to settingsRepository.exportJson()
        )
        val manifest = JSONObject().put("format", FORMAT).put("schema", 1)
            .put("appVersion", BuildConfig.VERSION_NAME).put("coreVersion", ProfileDeviceInfoProvider.CORE_VERSION)
            .put("createdAt", System.currentTimeMillis()).put("device", "${Build.MANUFACTURER} ${Build.MODEL}")
            .put("deviceId", DriveBackupState.get(context).value.deviceId)
        try {
            ZipOutputStream(file.outputStream().buffered()).use { zip ->
                suspend fun entry(name: String, input: InputStream) {
                    val hash = MessageDigest.getInstance("SHA-256")
                    zip.putNextEntry(ZipEntry(name).apply { time = 0 })
                    val size = input.use { copy(it, zip) { hash.update(it) } }
                    zip.closeEntry()
                    val hex = hash.digest().hex()
                    entries.put(JSONObject().put("name", name).put("size", size).put("sha256", hex))
                    contentHash.update("$name\u0000$size\u0000$hex\n".toByteArray())
                }
                for ((name, value) in json) entry(name, value.toString().byteInputStream())
                for ((name, source) in sources) entry(name, source.inputStream())
                manifest.put("files", entries)
                zip.putNextEntry(ZipEntry("manifest.json").apply { time = 0 })
                zip.write(manifest.toString().toByteArray())
                zip.closeEntry()
            }
            DriveSnapshot(file, contentHash.digest().hex(), manifest)
        } catch (error: Throwable) {
            file.delete()
            if (error is IOException) throw DriveBackupException(if (workDir.usableSpace < SPACE_RESERVE) "space" else "storage", error)
            throw error
        }
    }

    /** Validates every entry before any local state is changed. Extraction stays on private storage. */
    suspend fun extract(archive: File): JSONObject {
        val stage = File(workDir, "extracted")
        clearChild(workDir, stage); stage.mkdirs()
        try {
            ZipFile(archive).use { zip ->
                val metadata = zip.getEntry("manifest.json") ?: invalid()
                if (metadata.size !in 1..MAX_JSON_BYTES) invalid()
                val manifest = JSONObject(zip.getInputStream(metadata).use { it.readBytes().toString(Charsets.UTF_8) })
                if (manifest.optString("format") != FORMAT || manifest.optInt("schema") != 1) invalid()
                val files = manifest.getJSONArray("files")
                if (files.length() !in 1..100_000 || zip.size() != files.length() + 1) invalid()
                val seen = mutableSetOf<String>()
                var total = 0L
                for (i in 0 until files.length()) {
                    val spec = files.getJSONObject(i)
                    val name = spec.getString("name")
                    if (!allowed(name) || !seen.add(name)) invalid()
                    val size = spec.getLong("size")
                    if (size < 0 || size > Long.MAX_VALUE - total) invalid()
                    if (name in JSON_FILES && size > MAX_JSON_BYTES) invalid()
                    total += size
                }
                if (!seen.containsAll(JSON_FILES)) invalid()
                if (total > stage.usableSpace - SPACE_RESERVE) throw DriveBackupException("space")
                for (i in 0 until files.length()) {
                    currentCoroutineContext().ensureActive()
                    val spec = files.getJSONObject(i)
                    val name = spec.getString("name")
                    val entry = zip.getEntry(name) ?: invalid()
                    val size = spec.getLong("size")
                    if (entry.isDirectory || entry.size != size) invalid()
                    val target = File(stage, name).canonicalFile
                    if (!target.path.startsWith(stage.canonicalPath + File.separator)) invalid()
                    target.parentFile!!.mkdirs()
                    val hash = MessageDigest.getInstance("SHA-256")
                    val actual = target.outputStream().use { output ->
                        zip.getInputStream(entry).use { input -> copy(input, output, size) { hash.update(it) } }
                    }
                    if (actual != size || hash.digest().hex() != spec.getString("sha256")) invalid()
                }
                JSON_FILES.forEach { JSONObject(File(stage, it).readText()) }
                return manifest
            }
        } catch (error: Throwable) {
            clearChild(workDir, stage)
            throw when (error) {
                is CancellationException, is DriveBackupException -> error
                is ZipException, is JSONException -> DriveBackupException("invalid", error)
                is IOException -> DriveBackupException("storage", error)
                else -> error
            }
        }
    }

    suspend fun restore(manifest: JSONObject, categories: Set<String>) = BackupSessionGate.whileStopped {
        recoverPending()
        val stage = File(workDir, "extracted")
        val currentRoots = roots()
        clearChild(context.noBackupFilesDir, transaction); transaction.mkdirs()
        val old = File(transaction, "old").apply { mkdirs() }
        val files = manifest.getJSONArray("files")
        val changes = JSONArray()
        var oldSize = 0L
        for (i in 0 until files.length()) {
            val name = files.getJSONObject(i).getString("name")
            val prefix = name.substringBefore('/')
            val category = prefix
            if (category !in categories || prefix !in currentRoots) continue
            val root = currentRoots.getValue(prefix).canonicalFile
            val target = File(root, name.substringAfter('/')).canonicalFile
            if (!target.path.startsWith(root.path + File.separator)) invalid()
            if (target.isDirectory) invalid()
            oldSize += if (target.isFile) target.length() else 0
            changes.put(JSONObject().put("name", name).put("target", target.path).put("existed", target.isFile))
        }
        if (oldSize > old.usableSpace - SPACE_RESERVE) throw DriveBackupException("space")
        // Finish the complete rollback image before publishing a journal or changing any target.
        val oldSettings = settingsRepository.exportJson()
        writeAtomic(File(transaction, "settings.json"), oldSettings)
        for (i in 0 until changes.length()) {
            val change = changes.getJSONObject(i)
            if (change.getBoolean("existed")) {
                val saved = File(old, change.getString("name"))
                saved.parentFile!!.mkdirs()
                File(change.getString("target")).inputStream().use { input ->
                    saved.outputStream().use { output -> copy(input, output); output.fd.sync() }
                }
            }
        }
        val record = JSONObject().put("changes", changes).put("committed", false)
        writeAtomic(journal.baseFile, record)
        try {
            for (i in 0 until changes.length()) {
                currentCoroutineContext().ensureActive()
                val change = changes.getJSONObject(i)
                replaceFile(File(stage, change.getString("name")), File(change.getString("target")))
            }
            settingsRepository.restoreJson(JSONObject(File(stage, "settings.json").readText()),
                applyApp = "settings" in categories, applyCore = "settings" in categories,
                applyCustomization = "customization" in categories)
            writeAtomic(journal.baseFile, record.put("committed", true))
        } catch (error: Throwable) {
            withContext(NonCancellable) { recoverPending() }
            throw error
        }
        // Keep one private rollback image; a later restore replaces it only after recovery.
        journal.delete()
    }

    /** Also called before VM startup so a process death cannot expose a half-restored card. */
    suspend fun recoverPending() {
        if (!journal.baseFile.exists() && !File(transaction, "journal.json.bak").exists()) return
        val record = JSONObject(journal.openRead().bufferedReader().use { it.readText() })
        if (!record.optBoolean("committed")) {
            val changes = record.getJSONArray("changes")
            for (i in 0 until changes.length()) {
                val change = changes.getJSONObject(i)
                val target = File(change.getString("target"))
                if (change.getBoolean("existed")) replaceFile(File(transaction, "old/${change.getString("name")}"), target)
                else if (target.exists() && !target.delete()) throw DriveBackupException("restore")
            }
            settingsRepository.restoreJson(JSONObject(File(transaction, "settings.json").readText()))
        }
        journal.delete()
    }

    private fun replaceFile(source: File, target: File) {
        target.parentFile!!.mkdirs()
        val temporary = File(target.parentFile, ".${target.name}.drive-restore")
        try {
            source.inputStream().use { input -> temporary.outputStream().use { output -> input.copyTo(output); output.fd.sync() } }
            Files.move(temporary.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING)
        } finally { temporary.delete() }
    }

    private suspend fun copy(input: InputStream, output: OutputStream, limit: Long = Long.MAX_VALUE, consume: (ByteArray) -> Unit = {}): Long {
        val buffer = ByteArray(128 * 1024)
        var total = 0L
        while (true) {
            currentCoroutineContext().ensureActive()
            BackupSessionGate.checkpoint()
            val count = input.read(buffer)
            if (count < 0) break
            if (count.toLong() > limit - total) invalid()
            total += count
            output.write(buffer, 0, count)
            consume(buffer.copyOf(count))
        }
        return total
    }

    companion object {
        const val FORMAT = "emucorev-drive-backup"
        val ALL_CATEGORIES = setOf("settings", "customization", "save-data", "trophies", "per-game", "cheats")
        private val JSON_FILES = setOf("settings.json")
        private const val MAX_JSON_BYTES = 32L * 1024 * 1024
        private const val SPACE_RESERVE = 32L * 1024 * 1024
        fun hasPendingRecovery(context: Context): Boolean =
            File(context.noBackupFilesDir, "drive-restore/journal.json").exists() ||
                File(context.noBackupFilesDir, "drive-restore/journal.json.bak").exists()
        private fun invalid(): Nothing = throw DriveBackupException("invalid")
        fun ByteArray.hex(): String = joinToString("") { "%02x".format(it) }
        fun writeAtomic(file: File, json: JSONObject) {
            file.parentFile!!.mkdirs()
            val atomic = AtomicFile(file)
            val output = atomic.startWrite()
            try { output.write(json.toString().toByteArray()); atomic.finishWrite(output) }
            catch (error: Throwable) { atomic.failWrite(output); throw error }
        }
        fun clearChild(parent: File, child: File) {
            require(child.canonicalPath.startsWith(parent.canonicalPath + File.separator))
            if (child.exists() && !child.deleteRecursively()) throw DriveBackupException("storage")
        }
    }
}
