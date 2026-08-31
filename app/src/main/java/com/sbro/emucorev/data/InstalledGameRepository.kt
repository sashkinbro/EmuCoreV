package com.sbro.emucorev.data

import android.content.Context
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.core.VitaSfoParser
import java.io.File

class InstalledGameRepository {
    fun loadInstalledGames(context: Context): List<InstalledVitaGame> {
        val root = EmulatorStorage.ux0AppRoot(context)
        return root.listFiles().orEmpty()
            .filter(File::isDirectory)
            .map { directory ->
                val titleId = directory.name
                val metadata = VitaSfoParser.parse(EmulatorStorage.paramSfoPath(context, titleId))
                val iconFile = EmulatorStorage.iconPath(context, titleId)
                InstalledVitaGame(
                    titleId = metadata.titleId ?: titleId,
                    title = metadata.title ?: titleId,
                    contentId = metadata.contentId,
                    saveDataId = metadata.saveDataId ?: metadata.titleId ?: titleId,
                    version = metadata.version,
                    category = metadata.category,
                    iconPath = iconFile.takeIf { it.exists() }?.absolutePath,
                    installPath = directory.absolutePath
                )
            }
            .sortedBy { it.title.lowercase() }
    }

    fun findByTitleId(context: Context, titleId: String): InstalledVitaGame? {
        val safeId = titleId.trim().takeIf(::isSafePathSegment) ?: return null
        val directory = File(EmulatorStorage.ux0AppRoot(context), safeId)
        if (directory.isDirectory) {
            val metadata = VitaSfoParser.parse(File(directory, "sce_sys/param.sfo"))
            val resolvedId = metadata.titleId ?: safeId
            if (resolvedId.equals(safeId, ignoreCase = true)) {
                return InstalledVitaGame(
                    titleId = resolvedId,
                    title = metadata.title ?: safeId,
                    contentId = metadata.contentId,
                    saveDataId = metadata.saveDataId ?: resolvedId,
                    version = metadata.version,
                    category = metadata.category,
                    iconPath = File(directory, "sce_sys/icon0.png").takeIf(File::exists)?.absolutePath,
                    installPath = directory.absolutePath
                )
            }
        }
        // Preserve support for nonstandard install folders and case variants.
        return loadInstalledGames(context).firstOrNull { it.titleId.equals(titleId, ignoreCase = true) }
    }

    fun deleteByTitleId(context: Context, titleId: String): Boolean {
        val safeTitleId = titleId.trim().takeIf(::isSafePathSegment) ?: return false
        val deleted = mutableListOf<File>()
        val failed = mutableListOf<File>()

        findInstalledGameFolders(context, safeTitleId).forEach { appFolder ->
            deleteInstalledGameFiles(
                vitaRoot = appFolder.parentFile?.parentFile?.parentFile ?: return@forEach,
                titleSegment = appFolder.name,
                deleted = deleted,
                failed = failed
            )
        }

        EmulatorStorage.knownStorageRoots(context).forEach { storageRoot ->
            val vitaRoot = File(storageRoot, "vita")
            deleteInstalledGameFiles(
                vitaRoot = vitaRoot,
                titleSegment = safeTitleId,
                deleted = deleted,
                failed = failed
            )
        }

        return deleted.isNotEmpty() && failed.isEmpty() && findInstalledGameFolders(context, safeTitleId).isEmpty()
    }

    private fun findInstalledGameFolders(context: Context, titleId: String): Set<File> {
        return EmulatorStorage.knownStorageRoots(context)
            .map { storageRoot -> File(storageRoot, "vita/ux0/app") }
            .flatMap { appRoot -> appRoot.listFiles().orEmpty().filter(File::isDirectory) }
            .filter { directory ->
                directory.name.equals(titleId, ignoreCase = true) ||
                    VitaSfoParser.parse(File(directory, "sce_sys/param.sfo"))
                        .titleId
                        ?.equals(titleId, ignoreCase = true) == true
            }
            .toSet()
    }

    private fun deleteInstalledGameFiles(
        vitaRoot: File,
        titleSegment: String,
        deleted: MutableList<File>,
        failed: MutableList<File>
    ) {
        if (!isSafePathSegment(titleSegment)) return
        listOf(
            "ux0/app/$titleSegment",
            "ux0/appmeta/$titleSegment",
            "ux0/patch/$titleSegment",
            "ux0/addcont/$titleSegment",
            "ux0/license/app/$titleSegment"
        ).forEach { relativePath ->
            deleteRecursively(File(vitaRoot, relativePath), deleted, failed)
        }
    }

    private fun deleteRecursively(
        target: File,
        deleted: MutableList<File>,
        failed: MutableList<File>
    ) {
        if (!target.exists()) return
        val removed = runCatching { target.deleteRecursively() }.getOrDefault(false)
        if (removed && !target.exists()) {
            deleted += target
        } else {
            failed += target
        }
    }

    private fun isSafePathSegment(value: String): Boolean {
        return value.isNotBlank() &&
            value != "." &&
            value != ".." &&
            value.none { it == '/' || it == '\\' || it == File.separatorChar }
    }
}
