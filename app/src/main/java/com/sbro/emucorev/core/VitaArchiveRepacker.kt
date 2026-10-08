package com.sbro.emucorev.core

import android.util.Log
import org.apache.commons.compress.archivers.zip.ZipFile
import java.io.File
import java.io.IOException
import java.util.Locale
import java.util.zip.Deflater
import java.util.zip.CRC32
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

data class ArchiveRepackProgress(
    val progress: Float,
    val current: Int,
    val total: Int,
    val detail: String? = null
)

object VitaArchiveRepacker {
    private const val TAG = "VitaArchiveRepacker"
    private const val CACHE_DIR_NAME = "install_repack_cache"
    private const val VITAMIN_MARKER = "sce_module/steroid.suprx"

    fun canRepack(path: String): Boolean {
        val extension = File(path).extension.lowercase(Locale.US)
        return extension == "vpk" || extension == "zip"
    }

    fun repackToInstallZip(
        sourcePath: String,
        cacheRoot: File,
        onProgress: (ArchiveRepackProgress) -> Unit = {}
    ): File? {
        val source = File(sourcePath)
        if (!source.isFile || !source.canRead() || !canRepack(source.absolutePath)) return null

        var partialOutput: File? = null
        return runCatching {
            val outputDir = File(cacheRoot, CACHE_DIR_NAME).apply { mkdirs() }
            val output = File.createTempFile("${safeBaseName(source).take(64)}-".padEnd(3, '_'), "-repacked.zip", outputDir)
                .also { partialOutput = it }

            ZipFile.builder().setFile(source).get().use { inputZip ->
                val entries = inputZip.entries.asSequence().toList()
                if (entries.isEmpty()) {
                    throw IOException("Archive is empty")
                }
                logInfo("Repairing ${source.name}: ${entries.size} ZIP entries")

                val writtenNames = LinkedHashSet<String>()
                ZipOutputStream(output.outputStream().buffered()).use { zipOut ->
                    zipOut.setLevel(Deflater.DEFAULT_COMPRESSION)

                    entries.forEachIndexed { index, entry ->
                        val normalizedName = normalizeArchiveEntryName(entry.name)
                            ?: throw IOException("Unsafe archive entry: ${entry.name}")
                        if (isUnsupportedVitaminMarker(normalizedName)) {
                            logInfo("Skipping unsupported Vitamin marker entry: $normalizedName")
                            reportProgress(index + 1, entries.size, normalizedName, onProgress)
                            return@forEachIndexed
                        }
                        if (!writtenNames.add(normalizedName.trimEnd('/').lowercase(Locale.US)))
                            throw IOException("Ambiguous duplicate archive entry: $normalizedName")
                        if (normalizedName.isEmpty()) {
                            if (!entry.isDirectory) throw IOException("Empty archive filename")
                            reportProgress(index + 1, entries.size, entry.name, onProgress)
                            return@forEachIndexed
                        }
                        if (!inputZip.canReadEntryData(entry)) {
                            throw IOException("Unsupported ZIP method ${entry.method} for $normalizedName")
                        }

                        val outputEntry = ZipEntry(normalizedName).apply {
                            method = ZipEntry.DEFLATED
                            if (entry.time >= 0L) {
                                time = entry.time
                            }
                        }
                        zipOut.putNextEntry(outputEntry)
                        if (!entry.isDirectory) {
                            val crc = CRC32()
                            var copied = 0L
                            inputZip.getInputStream(entry).use { input ->
                                val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                                while (true) {
                                    val count = input.read(buffer)
                                    if (count < 0) break
                                    if (count == 0) continue
                                    zipOut.write(buffer, 0, count)
                                    crc.update(buffer, 0, count)
                                    copied += count
                                }
                            }
                            if (copied != entry.size || crc.value != entry.crc)
                                throw IOException("Corrupted archive entry: $normalizedName")
                        }
                        zipOut.closeEntry()
                        reportProgress(index + 1, entries.size, normalizedName, onProgress)
                    }
                }
            }

            output.takeIf { it.isFile && it.length() > 0L }?.also {
                logInfo("Repaired archive written: ${it.absolutePath} (${it.length()} bytes)")
            }
        }.onFailure { error ->
            partialOutput?.delete()
            logError("Failed to repack archive: $sourcePath", error)
        }.getOrNull()
    }

    internal fun normalizeArchiveEntryName(name: String): String? {
        val normalized = name.replace('\\', '/')
        if (normalized.isBlank()) return null
        if (normalized.startsWith("/") || Regex("^[A-Za-z]:").containsMatchIn(normalized)) return null
        val components = normalized.split('/')
        if (components.any { it == ".." }) return null
        val canonical = components.filter { it.isNotEmpty() && it != "." }.joinToString("/")
        if (canonical.isEmpty()) return if (normalized.endsWith('/')) "" else null
        return canonical + if (normalized.endsWith('/')) "/" else ""
    }

    internal fun isUnsupportedVitaminMarker(name: String): Boolean {
        val normalized = name.replace('\\', '/').lowercase(Locale.US)
        return normalized == VITAMIN_MARKER || normalized.endsWith("/$VITAMIN_MARKER")
    }

    private fun safeBaseName(source: File): String {
        val base = source.name.substringBeforeLast('.').ifBlank { "archive" }
        return base.replace(Regex("[^A-Za-z0-9._-]"), "_").ifBlank { "archive" }
    }

    private fun reportProgress(
        current: Int,
        total: Int,
        entryName: String,
        onProgress: (ArchiveRepackProgress) -> Unit
    ) {
        val progress = if (total > 0) current.toFloat() / total.toFloat() * 100f else 0f
        onProgress(
            ArchiveRepackProgress(
                progress = progress.coerceIn(0f, 100f),
                current = current,
                total = total,
                detail = entryName.takeIf { it.isNotBlank() }
            )
        )
    }

    private fun logInfo(message: String) {
        runCatching { Log.i(TAG, message) }
    }

    private fun logError(message: String, error: Throwable) {
        runCatching { Log.e(TAG, message, error) }
    }
}
