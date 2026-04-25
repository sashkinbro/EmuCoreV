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
        return loadInstalledGames(context).firstOrNull { it.titleId.equals(titleId, ignoreCase = true) }
    }

    fun deleteByTitleId(context: Context, titleId: String): Boolean {
        val game = findByTitleId(context, titleId) ?: return false
        return runCatching { File(game.installPath).deleteRecursively() }.getOrDefault(false)
    }
}
