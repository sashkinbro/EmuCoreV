package com.sbro.emucorev.core

import android.content.Context
import android.os.Environment
import com.sbro.emucorev.data.AppPreferences
import java.io.File

data class VitaStorageLocation(
    val rootPath: String,
    val vitaPath: String,
    val removable: Boolean,
    val selected: Boolean,
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
    /**
     * Stable location for configuration, logs, patches and transient cache.
     * Only the large Vita filesystem is allowed to move to removable storage.
     */
    fun runtimeRoot(context: Context): File {
        return (context.getExternalFilesDir(null) ?: context.filesDir).apply { mkdirs() }
    }

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
                selected = root.absolutePath == selectedRoot
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
        migrateLegacyRuntimeData(context, previousRoot)
        val migration = if (migrateExistingData && previousRoot.absolutePath != selectedRoot.absolutePath) {
            migrateRuntimeData(previousRoot, selectedRoot, onMigrationProgress)
        } else {
            StorageMigrationResult(
                sourceRootPath = previousRoot.absolutePath,
                targetRootPath = selectedRoot.absolutePath
            )
        }
        migrateLegacyCacheRoot(context)
        AppPreferences(context).vitaStorageRootPath = selectedRoot.absolutePath
        prepareRuntime(context)
        return migration
    }

    fun vitaRoot(context: Context): File {
        val base = storageRoot(context)
        return File(base, "vita").apply { mkdirs() }
    }

    fun cacheRoot(context: Context): File {
        return File(runtimeRoot(context), "cache").apply { mkdirs() }
    }

    /**
     * Staging area for install payloads (PKG/VPK/ZIP).
     *
     * These are game-sized, so they must follow the user's selected storage
     * root instead of always landing on internal storage. Keeping this separate
     * from [cacheRoot] means small runtime data (logs, shader cache, patches)
     * stays on stable internal storage while bulk transfers go to the SD card.
     */
    fun installStagingRoot(context: Context): File {
        return File(storageRoot(context), "cache/install_cache").apply { mkdirs() }
    }

    fun prepareRuntime(context: Context) {
        val runtimeRoot = runtimeRoot(context)
        migrateLegacyRuntimeData(context, storageRoot(context))
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
            File(runtimeRoot, "shaderlog"),
            File(runtimeRoot, "texturelog"),
            File(runtimeRoot, "patch"),
            File(runtimeRoot, "config"),
            File(cacheRoot, "shaders"),
            File(cacheRoot, "logs")
        ).forEach { directory ->
            if (!directory.exists()) {
                directory.mkdirs()
            }
        }

        purgeOrphanedInstallStaging(context)
    }

    /**
     * Removes install payloads left behind by earlier versions.
     *
     * Staging used to live on internal storage and was never cleaned up, so a
     * single game install could strand several GB there. Both the legacy
     * internal location and the current one are swept, since a staged file only
     * ever needs to outlive the install that created it.
     */
    private fun purgeOrphanedInstallStaging(context: Context) {
        val stagingDirs = buildSet {
            add(File(runtimeRoot(context), "cache/install_cache"))
            add(File(storageRoot(context), "cache/install_cache"))
        }

        stagingDirs.forEach { dir ->
            runCatching {
                if (!dir.isDirectory) return@runCatching
                dir.listFiles()?.forEach { staged ->
                    if (staged.isFile) staged.delete() else staged.deleteRecursively()
                }
            }
        }
    }

    fun ux0AppRoot(context: Context): File = File(vitaRoot(context), "ux0/app").apply { mkdirs() }

    /** Summary of a cache clear, in bytes freed and files removed. */
    data class CacheClearResult(
        val bytesFreed: Long,
        val filesRemoved: Int
    )

    /**
     * Reports the size of caches that [clearCaches] would remove.
     */
    fun cacheSizeBytes(context: Context): Long =
        clearableCacheDirs(context).sumOf { dir ->
            runCatching {
                if (!dir.isDirectory) 0L
                else dir.walkTopDown().filter { it.isFile }.sumOf { it.length() }
            }.getOrDefault(0L)
        }

    /**
     * Deletes regenerable caches only.
     *
     * Shader/texture caches, logs and install staging are all rebuilt on demand,
     * so removing them is always safe. Saves, installed games, firmware,
     * trophies, settings and GPU drivers are deliberately untouched.
     */
    fun clearCaches(context: Context): CacheClearResult {
        var bytesFreed = 0L
        var filesRemoved = 0

        clearableCacheDirs(context).forEach { dir ->
            runCatching {
                if (!dir.isDirectory) return@runCatching
                dir.listFiles()?.forEach { entry ->
                    val size = if (entry.isFile) {
                        entry.length()
                    } else {
                        entry.walkTopDown().filter { it.isFile }.sumOf { it.length() }
                    }
                    val count = if (entry.isFile) {
                        1
                    } else {
                        entry.walkTopDown().count { it.isFile }
                    }
                    val deleted = if (entry.isFile) entry.delete() else entry.deleteRecursively()
                    if (deleted) {
                        bytesFreed += size
                        filesRemoved += count
                    }
                }
            }
        }

        // Recreate the directory skeleton so the core does not have to.
        prepareRuntime(context)

        return CacheClearResult(bytesFreed = bytesFreed, filesRemoved = filesRemoved)
    }

    /**
     * Cache directories that are safe to delete.
     *
     * Both storage roots are covered because the selected root may have changed
     * since the cache was written.
     */
    private fun clearableCacheDirs(context: Context): List<File> {
        val runtimeRoot = runtimeRoot(context)
        val storageRoot = storageRoot(context)
        return buildSet {
            add(File(runtimeRoot, "cache"))
            add(File(storageRoot, "cache"))
            add(File(runtimeRoot, "shaderlog"))
            add(File(runtimeRoot, "texturelog"))
            add(context.cacheDir)
            context.externalCacheDir?.let(::add)
        }.toList()
    }

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
        val migrationItems = listOf("vita")
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

    private fun migrateLegacyCacheRoot(context: Context) {
        val legacyBase = context.externalCacheDir ?: context.cacheDir
        val legacyCache = File(legacyBase, "vita_cache")
        val targetCache = cacheRoot(context)
        if (legacyCache.exists() && legacyCache.absolutePath != targetCache.absolutePath) {
            copyMissing(legacyCache, targetCache) { _, _, _ -> }
        }
    }

    private fun migrateLegacyRuntimeData(context: Context, legacyRoot: File) {
        val targetRoot = runtimeRoot(context)
        if (!legacyRoot.exists() || legacyRoot.absolutePath == targetRoot.absolutePath) return
        listOf("cache", "patch", "shaderlog", "texturelog", "config.yml", "config", "play_time.json")
            .forEach { name ->
                val source = File(legacyRoot, name)
                if (source.exists()) {
                    copyNewer(source, File(targetRoot, name))
                }
            }
    }

    private fun copyNewer(source: File, target: File) {
        if (source.isDirectory) {
            target.mkdirs()
            source.listFiles().orEmpty().forEach { child ->
                copyNewer(child, File(target, child.name))
            }
            return
        }
        if (target.exists() && target.lastModified() >= source.lastModified()) return
        target.parentFile?.mkdirs()
        source.copyTo(target, overwrite = true)
    }

}
