package com.sbro.emucorev.core

import android.content.Context
import android.net.Uri
import android.os.Environment
import android.os.storage.StorageManager
import android.provider.DocumentsContract
import android.system.Os
import androidx.documentfile.provider.DocumentFile
import com.sbro.emucorev.data.AppPreferences
import java.io.File

data class VitaStorageLocation(
    val rootPath: String,
    val vitaPath: String,
    val removable: Boolean,
    val selected: Boolean,
    val custom: Boolean = false
)

data class StorageMigrationResult(
    val sourceRootPath: String,
    val targetRootPath: String,
    val copiedFiles: Int = 0,
    val skippedFiles: Int = 0
)

data class StorageMigrationProgress(
    val sourceRootPath: String,
    val targetRootPath: String,
    val copiedFiles: Int,
    val skippedFiles: Int,
    val totalFiles: Int,
    val currentPath: String?
)

object EmulatorStorage {
    fun storageRoot(context: Context): File {
        val roots = availableStorageRoots(context)
        val selected = AppPreferences(context).vitaStorageRootPath
            ?.let(::File)
            ?.takeIf { configured -> roots.any { it.absolutePath == configured.absolutePath } }
        return selected ?: roots.firstOrNull() ?: context.filesDir
    }

    private fun availableStorageRoots(context: Context): List<File> {
        val roots = linkedMapOf<String, File>()
        context.getExternalFilesDir(null)?.let { roots[it.absolutePath] = it }
        context.getExternalFilesDirs(null).filterNotNull().forEach { root ->
            roots[root.absolutePath] = root
        }
        AppPreferences(context).vitaCustomStorageRootPath
            ?.let(::File)
            ?.takeIf { it.canBeRuntimeRoot() }
            ?.let { roots[it.absolutePath] = it }
        if (roots.isEmpty()) {
            roots[context.filesDir.absolutePath] = context.filesDir
        }
        return roots.values.toList()
    }

    fun knownStorageRoots(context: Context): List<File> = availableStorageRoots(context)

    fun availableStorageLocations(context: Context): List<VitaStorageLocation> {
        val selectedRoot = storageRoot(context).absolutePath
        return availableStorageRoots(context).map { root ->
            VitaStorageLocation(
                rootPath = root.absolutePath,
                vitaPath = File(root, "vita").absolutePath,
                removable = runCatching { Environment.isExternalStorageRemovable(root) }.getOrDefault(false),
                selected = root.absolutePath == selectedRoot,
                custom = root.absolutePath == AppPreferences(context).vitaCustomStorageRootPath
            )
        }
    }

    fun selectStorageRoot(
        context: Context,
        rootPath: String,
        migrateExistingData: Boolean = false,
        onMigrationProgress: ((StorageMigrationProgress) -> Unit)? = null
    ): StorageMigrationResult {
        val selectedRoot = availableStorageRoots(context).firstOrNull { it.absolutePath == rootPath }
            ?: return StorageMigrationResult(
                sourceRootPath = storageRoot(context).absolutePath,
                targetRootPath = storageRoot(context).absolutePath
            )
        val previousRoot = storageRoot(context)
        val migration = if (migrateExistingData && previousRoot.absolutePath != selectedRoot.absolutePath) {
            migrateRuntimeData(previousRoot, selectedRoot, onMigrationProgress)
        } else {
            StorageMigrationResult(
                sourceRootPath = previousRoot.absolutePath,
                targetRootPath = selectedRoot.absolutePath
            )
        }
        migrateLegacyCacheRoot(context, selectedRoot)
        AppPreferences(context).vitaStorageRootPath = selectedRoot.absolutePath
        prepareRuntime(context)
        return migration
    }

    fun selectCustomStorageRoot(
        context: Context,
        treeUri: Uri,
        migrateExistingData: Boolean = false,
        onMigrationProgress: ((StorageMigrationProgress) -> Unit)? = null
    ): StorageMigrationResult {
        val selectedRoot = resolveTreeUriToFile(context, treeUri)
            ?.takeIf { it.canBeRuntimeRoot() }
            ?: throw IllegalArgumentException("Selected folder is not available as emulator storage.")

        AppPreferences(context).setVitaCustomStorageRoot(context, treeUri, selectedRoot.absolutePath)
        return selectStorageRoot(
            context = context,
            rootPath = selectedRoot.absolutePath,
            migrateExistingData = migrateExistingData,
            onMigrationProgress = onMigrationProgress
        )
    }

    fun vitaRoot(context: Context): File {
        val base = storageRoot(context)
        return File(base, "vita").apply { mkdirs() }
    }

    fun cacheRoot(context: Context): File {
        return File(storageRoot(context), "cache").apply { mkdirs() }
    }

    fun prepareRuntime(context: Context) {
        val storageRoot = storageRoot(context)
        val vitaRoot = vitaRoot(context)
        val cacheRoot = cacheRoot(context)
        listOf(
            vitaRoot,
            cacheRoot,
            File(vitaRoot, "ux0"),
            File(vitaRoot, "ux0/app"),
            File(vitaRoot, "ux0/data"),
            File(vitaRoot, "ux0/user"),
            File(vitaRoot, "vs0"),
            File(storageRoot, "shaderlog"),
            File(cacheRoot, "shaders"),
            File(cacheRoot, "logs")
        ).forEach { directory ->
            if (!directory.exists()) {
                directory.mkdirs()
            }
        }
    }

    fun ux0AppRoot(context: Context): File = File(vitaRoot(context), "ux0/app").apply { mkdirs() }

    fun ux0SaveDataRoot(context: Context, userId: String? = null): File {
        val userSegment = userId?.takeIf(String::isNotBlank)
        val relativePath = if (userSegment == null) {
            "ux0/user/savedata"
        } else {
            "ux0/user/$userSegment/savedata"
        }
        return File(vitaRoot(context), relativePath).apply { mkdirs() }
    }

    fun hasInstalledFirmware(context: Context): Boolean {
        val firmwareRoot = File(vitaRoot(context), "vs0")
        if (!firmwareRoot.exists() || !firmwareRoot.isDirectory) return false
        return firmwareRoot.walkTopDown().any { it.isFile }
    }

    fun hasInstalledFirmwareUpdate(context: Context): Boolean {
        val updateRoot = File(vitaRoot(context), "sa0")
        if (!updateRoot.exists() || !updateRoot.isDirectory) return false
        return updateRoot.walkTopDown().any { it.isFile }
    }

    fun iconPath(context: Context, titleId: String): File =
        File(ux0AppRoot(context), "$titleId/sce_sys/icon0.png")

    fun paramSfoPath(context: Context, titleId: String): File =
        File(ux0AppRoot(context), "$titleId/sce_sys/param.sfo")

    private fun migrateRuntimeData(
        sourceRoot: File,
        targetRoot: File,
        onProgress: ((StorageMigrationProgress) -> Unit)?
    ): StorageMigrationResult {
        if (!sourceRoot.exists() || sourceRoot.absolutePath == targetRoot.absolutePath) {
            return StorageMigrationResult(sourceRoot.absolutePath, targetRoot.absolutePath)
        }
        targetRoot.mkdirs()
        val migrationItems = listOf("vita", "cache", "patch", "shaderlog", "config.yml", "config", "play_time.json")
        val totalFiles = migrationItems.sumOf { name -> File(sourceRoot, name).countFiles() }
        onProgress?.invoke(
            StorageMigrationProgress(
                sourceRootPath = sourceRoot.absolutePath,
                targetRootPath = targetRoot.absolutePath,
                copiedFiles = 0,
                skippedFiles = 0,
                totalFiles = totalFiles,
                currentPath = null
            )
        )
        var copied = 0
        var skipped = 0
        migrationItems.forEach { name ->
            val source = File(sourceRoot, name)
            if (source.exists()) {
                copyMissing(source, File(targetRoot, name)) { copiedDelta, skippedDelta, current ->
                    copied += copiedDelta
                    skipped += skippedDelta
                    onProgress?.invoke(
                        StorageMigrationProgress(
                            sourceRootPath = sourceRoot.absolutePath,
                            targetRootPath = targetRoot.absolutePath,
                            copiedFiles = copied,
                            skippedFiles = skipped,
                            totalFiles = totalFiles,
                            currentPath = current.relativeToOrSelf(sourceRoot).path
                        )
                    )
                }
            }
        }
        return StorageMigrationResult(
            sourceRootPath = sourceRoot.absolutePath,
            targetRootPath = targetRoot.absolutePath,
            copiedFiles = copied,
            skippedFiles = skipped
        )
    }

    private fun copyMissing(
        source: File,
        target: File,
        onFileVisited: (copiedDelta: Int, skippedDelta: Int, current: File) -> Unit
    ): Pair<Int, Int> {
        if (source.isDirectory) {
            if (!target.exists()) {
                target.mkdirs()
            }
            var copied = 0
            var skipped = 0
            source.listFiles().orEmpty().forEach { child ->
                val result = copyMissing(child, File(target, child.name), onFileVisited)
                copied += result.first
                skipped += result.second
            }
            return copied to skipped
        }
        if (target.exists()) {
            onFileVisited(0, 1, source)
            return 0 to 1
        }
        target.parentFile?.mkdirs()
        source.copyTo(target, overwrite = false)
        onFileVisited(1, 0, source)
        return 1 to 0
    }

    private fun File.countFiles(): Int {
        if (!exists()) return 0
        if (isFile) return 1
        return listFiles().orEmpty().sumOf { it.countFiles() }
    }

    private fun migrateLegacyCacheRoot(context: Context, targetRoot: File) {
        val legacyBase = context.externalCacheDir ?: context.cacheDir
        val legacyCache = File(legacyBase, "vita_cache")
        val targetCache = File(targetRoot, "cache")
        if (legacyCache.exists() && legacyCache.absolutePath != targetCache.absolutePath) {
            copyMissing(legacyCache, targetCache) { _, _, _ -> }
        }
    }

    private fun File.canBeRuntimeRoot(): Boolean {
        if (exists() && !isDirectory) return false
        if (!exists() && !mkdirs()) return false
        if (!canRead()) return false
        return runCatching {
            val testFile = File(this, ".emucorev_storage_write_test")
            if (testFile.exists()) {
                testFile.delete()
            }
            try {
                testFile.writeText("ok")
                testFile.isFile
            } finally {
                testFile.delete()
            }
        }.getOrDefault(false)
    }

    private fun resolveTreeUriToFile(context: Context, treeUri: Uri): File? {
        resolveTreeUriFromDescriptor(context, treeUri)?.let { return it }

        val rawPath = treeUri.path
            ?.removePrefix("/tree/raw:")
            ?.takeIf { it.startsWith("/") }
            ?.let(::File)
        if (rawPath != null) return rawPath

        val documentId = runCatching { DocumentsContract.getTreeDocumentId(treeUri) }.getOrNull()
            ?: return null
        val separatorIndex = documentId.indexOf(':')
        val volumeId = if (separatorIndex >= 0) documentId.substring(0, separatorIndex) else documentId
        val relativePath = if (separatorIndex >= 0) documentId.substring(separatorIndex + 1) else ""

        if (volumeId.equals("primary", ignoreCase = true)) {
            val root = Environment.getExternalStorageDirectory()
            return if (relativePath.isBlank()) root else File(root, relativePath)
        }

        resolveStorageVolumeRoot(context, volumeId)?.let { root ->
            return if (relativePath.isBlank()) root else File(root, relativePath)
        }

        return File("/storage/$volumeId").takeIf { it.exists() }
            ?.let { root -> if (relativePath.isBlank()) root else File(root, relativePath) }
    }

    private fun resolveTreeUriFromDescriptor(context: Context, treeUri: Uri): File? {
        val documentUri = DocumentFile.fromTreeUri(context, treeUri)?.uri ?: treeUri
        return runCatching {
            context.contentResolver.openFileDescriptor(documentUri, "r")?.use { descriptor ->
                Os.readlink("/proc/self/fd/${descriptor.fd}")
                    .normalizeAndroidStoragePath()
                    .takeIf { it.startsWith("/") }
                    ?.let(::File)
            }
        }.getOrNull()
    }

    private fun String.normalizeAndroidStoragePath(): String {
        if (!startsWith("/mnt/user/")) return this
        val withoutUserPrefix = substring("/mnt/user/".length)
        val storageSeparator = withoutUserPrefix.indexOf('/')
        if (storageSeparator < 0) return this
        return "/storage" + withoutUserPrefix.substring(storageSeparator)
    }

    private fun resolveStorageVolumeRoot(context: Context, volumeId: String): File? {
        val storageManager = context.getSystemService(StorageManager::class.java) ?: return null
        return storageManager.storageVolumes.firstNotNullOfOrNull { volume ->
            val uuid = runCatching { volume.uuid }.getOrNull()
            if (!uuid.equals(volumeId, ignoreCase = true)) {
                return@firstNotNullOfOrNull null
            }
            runCatching {
                volume.javaClass.getMethod("getPathFile").invoke(volume) as? File
            }.getOrNull()
        }
    }
}
