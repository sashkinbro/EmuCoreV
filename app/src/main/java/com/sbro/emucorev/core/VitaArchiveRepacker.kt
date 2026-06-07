package com.sbro.emucorev.core

import android.util.Log
import org.apache.commons.compress.archivers.zip.ZipFile
import java.io.File
import java.io.IOException
import java.util.Locale
import java.util.zip.Deflater
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

        return runCatching {
            val outputDir = File(cacheRoot, CACHE_DIR_NAME).apply { mkdirs() }
            val output = File(outputDir, "${safeBaseName(source)}-repacked.zip")
            if (output.exists() && !output.delete()) {
                throw IOException("Could not replace ${output.name}")
            }

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
                        if (normalizedName.isBlank() || !writtenNames.add(normalizedName)) {
                            reportProgress(index + 1, entries.size, normalizedName, onProgress)
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
                            inputZip.getInputStream(entry).use { input ->
                                input.copyTo(zipOut)
                            }
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
            logError("Failed to repack archive: $sourcePath", error)
        }.getOrNull()
    }

    internal fun normalizeArchiveEntryName(name: String): String? {
        val normalized = name.replace('\\', '/').trim()
        if (normalized.isBlank()) return null
        if (normalized.startsWith("/") || normalized.startsWith("../") || normalized == "..") return null
        if (normalized.contains("/../") || normalized.endsWith("/..")) return null
        return normalized
    }

    internal fun isUnsupportedVitaminMarker(name: String): Boolean {
        return name.replace('\\', '/').lowercase(Locale.US).contains(VITAMIN_MARKER)
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
