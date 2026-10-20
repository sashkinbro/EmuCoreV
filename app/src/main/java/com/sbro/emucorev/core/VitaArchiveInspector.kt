package com.sbro.emucorev.core

import org.apache.commons.compress.archivers.zip.ZipFile
import org.apache.commons.compress.archivers.zip.ZipArchiveEntry
import java.io.File
import java.util.Locale
import java.util.zip.ZipEntry
import java.util.zip.CRC32

data class VitaArchiveInspection(
    val readable: Boolean,
    val supportedExtension: Boolean,
    val vitaminDump: Boolean,
    val hasInstallMetadata: Boolean,
    val unsupportedCompressionEntries: Int,
    val errorMessage: String? = null,
    val patchTitleIds: Set<String> = emptySet()
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
                val patchTitleIds = linkedSetOf<String>()

                zip.entries.asSequence().forEach { entry ->
                    val name = VitaArchiveRepacker.normalizeArchiveEntryName(entry.name)
                        ?.lowercase(Locale.US) ?: entry.name.replace('\\', '/').lowercase(Locale.US)
                    if (name == VITAMIN_MARKER || name.endsWith("/$VITAMIN_MARKER")) {
                        vitaminDump = true
                    }
                    if (name == SFO_MARKER || name.endsWith("/$SFO_MARKER") ||
                        name == THEME_MARKER || name.endsWith("/$THEME_MARKER")) {
                        hasInstallMetadata = true
                    }
                    if (!entry.isDirectory && (name == SFO_MARKER || name.endsWith("/$SFO_MARKER"))) {
                        val metadata = readMetadata(zip, entry)
                        val titleId = metadata?.titleId
                        if (metadata?.category?.contains("gp") == true && titleId != null &&
                            Regex("[A-Z]{4}[0-9]{5}").matches(titleId)) patchTitleIds += titleId
                    }
                    if (
                        !entry.isDirectory &&
                        entry.method != ZipEntry.STORED &&
                        entry.method != ZipEntry.DEFLATED
                    ) {
                        unsupportedCompressionEntries++
                    }
                }

                VitaArchiveInspection(
                    readable = true,
                    supportedExtension = supportedExtension,
                    vitaminDump = vitaminDump,
                    hasInstallMetadata = hasInstallMetadata,
                    unsupportedCompressionEntries = unsupportedCompressionEntries,
                    patchTitleIds = patchTitleIds
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

    private fun readMetadata(zip: ZipFile, entry: ZipArchiveEntry): VitaSfoData? = runCatching {
        if (entry.size !in 20L..(1024L * 1024) || !zip.canReadEntryData(entry)) return null
        val bytes = ByteArray(entry.size.toInt())
        zip.getInputStream(entry).use { input ->
            var offset = 0
            while (offset < bytes.size) {
                val count = input.read(bytes, offset, bytes.size - offset)
                if (count < 0) return null
                if (count == 0) continue
                offset += count
            }
            if (input.read() != -1 || CRC32().apply { update(bytes) }.value != entry.crc) return null
        }
        VitaSfoParser.parse(bytes)
    }.getOrNull()
}
