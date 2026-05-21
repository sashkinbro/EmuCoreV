package com.sbro.emucorev.core

import android.content.Context
import android.os.Environment
import com.sbro.emucorev.data.AppPreferences
import java.io.File

data class VitaStorageLocation(
    val rootPath: String,
    val vitaPath: String,
    val removable: Boolean,
    val selected: Boolean
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
        if (roots.isEmpty()) {
            roots[context.filesDir.absolutePath] = context.filesDir
        }
        return roots.values.toList()
    }

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
        val migration = if (migrateExistingData && previousRoot.absolutePath != selectedRoot.absolutePath) {
            migrateRuntimeData(previousRoot, selectedRoot, onMigrationProgress)
        } else {
            StorageMigrationResult(
                sourceRootPath = previousRoot.absolutePath,
                targetRootPath = selectedRoot.absolutePath
            )
        }
        AppPreferences(context).vitaStorageRootPath = selectedRoot.absolutePath
        prepareRuntime(context)
        return migration
    }

    fun vitaRoot(context: Context): File {
        val base = storageRoot(context)
        return File(base, "vita").apply { mkdirs() }
    }

    fun cacheRoot(context: Context): File {
        val base = context.externalCacheDir ?: context.cacheDir
        return File(base, "vita_cache").apply { mkdirs() }
    }

    fun prepareRuntime(context: Context) {
        val storageRoot = storageRoot(context)
        val vitaRoot = vitaRoot(context)
        val cacheRoot = cacheRoot(context)
        val nativeCacheRoot = File(storageRoot, "cache")
        listOf(
            vitaRoot,
            cacheRoot,
            nativeCacheRoot,
            File(vitaRoot, "ux0"),
            File(vitaRoot, "ux0/app"),
            File(vitaRoot, "ux0/data"),
            File(vitaRoot, "ux0/user"),
            File(vitaRoot, "vs0"),
            File(storageRoot, "shaderlog"),
            File(nativeCacheRoot, "shaders"),
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
        val migrationItems = listOf("vita", "cache", "patch", "shaderlog", "config.yml", "config")
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
}
