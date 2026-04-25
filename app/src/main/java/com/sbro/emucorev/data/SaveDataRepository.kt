package com.sbro.emucorev.data

import android.content.Context
import android.net.Uri
import com.sbro.emucorev.core.EmulatorStorage
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.util.UUID
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

data class VitaSaveDataEntry(
    val saveId: String,
    val titleId: String?,
    val title: String,
    val iconPath: String?,
    val path: String,
    val sizeBytes: Long,
    val updatedAtMillis: Long,
    val installed: Boolean
)

data class VitaSaveDataTarget(
    val saveId: String,
    val titleId: String,
    val title: String,
    val iconPath: String?
)

sealed class SaveDataImportResult {
    data class Success(val saveId: String) : SaveDataImportResult()
    data object EmptyArchive : SaveDataImportResult()
    data object UnsafeArchive : SaveDataImportResult()
    data object UnknownTarget : SaveDataImportResult()
    data class Failure(val error: Throwable) : SaveDataImportResult()
}

class SaveDataRepository {
    private val installedGameRepository = InstalledGameRepository()

    fun list(context: Context): List<VitaSaveDataEntry> {
        val installedGames = installedGameRepository.loadInstalledGames(context)
        val gamesBySaveId = installedGames
            .mapNotNull { game -> game.saveDataId?.takeIf(String::isNotBlank)?.let { it to game } }
            .toMap()
        return saveRoots(context)
            .flatMap { saveRoot -> saveRoot.listFiles().orEmpty().toList() }
            .filter { it.isDirectory && it.listFiles().orEmpty().isNotEmpty() }
            .distinctBy { it.name }
            .map { directory ->
                val saveId = directory.name
                val game = gamesBySaveId[saveId] ?: installedGames.firstOrNull { it.titleId == saveId }
                VitaSaveDataEntry(
                    saveId = saveId,
                    titleId = game?.titleId,
                    title = game?.title ?: saveId,
                    iconPath = game?.iconPath,
                    path = directory.absolutePath,
                    sizeBytes = directory.directorySize(),
                    updatedAtMillis = directory.latestModified(),
                    installed = game != null
                )
            }
            .sortedWith(compareByDescending<VitaSaveDataEntry> { it.installed }.thenBy { it.title.lowercase() })
    }

    fun findForTitleId(context: Context, titleId: String): VitaSaveDataEntry? {
        val saveId = targetForTitleId(context, titleId)?.saveId ?: titleId
        return list(context).firstOrNull { it.saveId == saveId || it.titleId == titleId }
    }

    fun targetForTitleId(context: Context, titleId: String): VitaSaveDataTarget? {
        val game = installedGameRepository.findByTitleId(context, titleId) ?: return null
        return VitaSaveDataTarget(
            saveId = game.saveDataId?.takeIf(String::isNotBlank) ?: game.titleId,
            titleId = game.titleId,
            title = game.title,
            iconPath = game.iconPath
        )
    }

    fun delete(context: Context, saveId: String): Boolean {
        val targets = saveRoots(context).map { root -> File(root, saveId) }.filter(File::exists)
        if (targets.isEmpty()) return true
        return targets.all { target -> runCatching { target.deleteRecursively() }.getOrDefault(false) }
    }

    fun exportToZip(context: Context, saveId: String, destination: Uri): Result<Unit> = runCatching {
        val source = saveRoots(context)
            .map { root -> File(root, saveId) }
            .firstOrNull { it.isDirectory }
            ?: File(EmulatorStorage.ux0SaveDataRoot(context), saveId)
        require(source.isDirectory) { "Save data not found." }
        context.contentResolver.openOutputStream(destination)?.use { output ->
            ZipOutputStream(output.buffered()).use { zip ->
                source.zipInto(zip, "$saveId/")
            }
        } ?: error("Cannot open destination.")
    }

    fun importFromZip(context: Context, source: Uri, targetSaveId: String? = null): SaveDataImportResult {
        val importRoot = File(EmulatorStorage.cacheRoot(context), "save_import").apply {
            deleteRecursively()
            mkdirs()
        }
        val extractedRoot = File(importRoot, UUID.randomUUID().toString()).apply { mkdirs() }
        return try {
            val safe = context.contentResolver.openInputStream(source)?.use { input ->
                ZipInputStream(input.buffered()).use { zip -> zip.extractSafelyTo(extractedRoot) }
            } ?: return SaveDataImportResult.Failure(IllegalStateException("Cannot open archive."))
            if (!safe) return SaveDataImportResult.UnsafeArchive
            if (extractedRoot.walkTopDown().none { it.isFile }) return SaveDataImportResult.EmptyArchive

            val singleRoot = extractedRoot.listFiles().orEmpty().singleOrNull { it.isDirectory }
                ?.takeIf { root -> extractedRoot.listFiles().orEmpty().all { it == root } }
            val saveId = targetSaveId?.takeIf(String::isNotBlank)
                ?: singleRoot?.name?.takeIf(String::isNotBlank)
                ?: return SaveDataImportResult.UnknownTarget
            val contentRoot = when {
                targetSaveId != null && singleRoot != null -> singleRoot
                singleRoot != null && targetSaveId == null -> singleRoot
                else -> extractedRoot
            }
            replaceSaveDirectory(context, saveId, contentRoot)
            SaveDataImportResult.Success(saveId)
        } catch (error: Throwable) {
            SaveDataImportResult.Failure(error)
        } finally {
            importRoot.deleteRecursively()
        }
    }

    private fun replaceSaveDirectory(context: Context, saveId: String, source: File) {
        val saveRoot = EmulatorStorage.ux0SaveDataRoot(context)
        val target = File(saveRoot, saveId)
        val backup = File(EmulatorStorage.cacheRoot(context), "save_backup/$saveId-${System.currentTimeMillis()}")
        runCatching {
            if (target.exists()) {
                backup.parentFile?.mkdirs()
                target.copyRecursively(backup, overwrite = true)
                target.deleteRecursively()
            }
            target.mkdirs()
            source.copyRecursively(target, overwrite = true)
            backup.deleteRecursively()
        }.onFailure { error ->
            target.deleteRecursively()
            if (backup.exists()) {
                backup.copyRecursively(target, overwrite = true)
                backup.deleteRecursively()
            }
            throw error
        }
    }

    private fun saveRoots(context: Context): List<File> =
        listOf(
            EmulatorStorage.ux0SaveDataRoot(context),
            EmulatorStorage.ux0SaveDataRoot(context, "00")
        ).distinctBy { it.absolutePath }

    private fun ZipInputStream.extractSafelyTo(destination: File): Boolean {
        val destinationPath = destination.canonicalFile.toPath()
        var entry = nextEntry
        while (entry != null) {
            val outputFile = File(destination, entry.name).canonicalFile
            if (!outputFile.toPath().startsWith(destinationPath)) {
                closeEntry()
                return false
            }
            if (entry.isDirectory) {
                outputFile.mkdirs()
            } else {
                outputFile.parentFile?.mkdirs()
                FileOutputStream(outputFile).use { output -> copyTo(output) }
            }
            closeEntry()
            entry = nextEntry
        }
        return true
    }

    private fun File.zipInto(zip: ZipOutputStream, entryPrefix: String) {
        listFiles().orEmpty().forEach { child ->
            val entryName = entryPrefix + child.name
            if (child.isDirectory) {
                zip.putNextEntry(ZipEntry("$entryName/"))
                zip.closeEntry()
                child.zipInto(zip, "$entryName/")
            } else {
                zip.putNextEntry(ZipEntry(entryName))
                FileInputStream(child).use { input -> input.copyTo(zip) }
                zip.closeEntry()
            }
        }
    }

    private fun File.directorySize(): Long =
        walkTopDown().filter { it.isFile }.sumOf { it.length() }

    private fun File.latestModified(): Long =
        walkTopDown().maxOfOrNull { it.lastModified() } ?: lastModified()
}
