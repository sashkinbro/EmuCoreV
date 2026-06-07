package com.sbro.emucorev.core

import org.apache.commons.compress.archivers.zip.ZipFile
import java.io.File
import java.util.Locale
import java.util.zip.ZipEntry

data class VitaArchiveInspection(
    val readable: Boolean,
    val supportedExtension: Boolean,
    val vitaminDump: Boolean,
    val hasInstallMetadata: Boolean,
    val unsupportedCompressionEntries: Int,
    val errorMessage: String? = null
) {
    val supportedCompression: Boolean
        get() = unsupportedCompressionEntries == 0
}

object VitaArchiveInspector {
    private const val VITAMIN_MARKER = "sce_module/steroid.suprx"
    private const val SFO_MARKER = "sce_sys/param.sfo"
    private const val THEME_MARKER = "theme.xml"

    fun isVitaminDump(path: String): Boolean {
        return inspect(path).vitaminDump
    }

    fun inspect(path: String): VitaArchiveInspection {
        val archive = File(path)
        val extension = archive.extension.lowercase(Locale.US)
        val supportedExtension = extension == "vpk" || extension == "zip"
        if (!archive.isFile || !archive.canRead()) {
            return VitaArchiveInspection(
                readable = false,
                supportedExtension = supportedExtension,
                vitaminDump = false,
                hasInstallMetadata = false,
                unsupportedCompressionEntries = 0
            )
        }

        return runCatching {
            ZipFile.builder().setFile(archive).get().use { zip ->
                var vitaminDump = false
                var hasInstallMetadata = false
                var unsupportedCompressionEntries = 0

                zip.entries.asSequence().forEach { entry ->
                    val name = entry.name.replace('\\', '/').lowercase(Locale.US)
                    if (name.contains(VITAMIN_MARKER)) {
                        vitaminDump = true
                    }
                    if (name.contains(SFO_MARKER) || name.endsWith(THEME_MARKER)) {
                        hasInstallMetadata = true
                    }
                    if (
                        !entry.isDirectory &&
                        entry.method != ZipEntry.STORED &&
                        entry.method != ZipEntry.DEFLATED &&
                        !zip.canReadEntryData(entry)
                    ) {
                        unsupportedCompressionEntries++
                    }
                }

                VitaArchiveInspection(
                    readable = true,
                    supportedExtension = supportedExtension,
                    vitaminDump = vitaminDump,
                    hasInstallMetadata = hasInstallMetadata,
                    unsupportedCompressionEntries = unsupportedCompressionEntries
                )
            }
        }.getOrElse { error ->
            VitaArchiveInspection(
                readable = false,
                supportedExtension = supportedExtension,
                vitaminDump = false,
                hasInstallMetadata = false,
                unsupportedCompressionEntries = 0,
                errorMessage = error.message
            )
        }
    }
}
