package com.sbro.emucorev.core

import java.io.File
import java.util.zip.ZipFile

object VitaArchiveInspector {
    private const val VITAMIN_MARKER = "sce_module/steroid.suprx"

    fun isVitaminDump(path: String): Boolean {
        val archive = File(path)
        if (!archive.isFile || !archive.canRead()) return false

        return runCatching {
            ZipFile(archive).use { zip ->
                zip.entries().asSequence().any { entry ->
                    entry.name
                        .replace('\\', '/')
                        .lowercase()
                        .contains(VITAMIN_MARKER)
                }
            }
        }.getOrDefault(false)
    }
}
