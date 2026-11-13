package com.sbro.emucorev.core

import android.util.Log
import org.apache.commons.compress.archivers.zip.ZipFile
import org.apache.commons.compress.archivers.zip.ZipArchiveEntry
import java.io.File
import java.io.IOException
import java.util.Locale
import java.util.zip.Deflater
import java.util.zip.CRC32
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import java.nio.file.FileVisitResult
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.Path
import java.nio.file.SimpleFileVisitor
import java.nio.file.attribute.BasicFileAttributes
import java.util.UUID

data class ArchiveRepackProgress(
    val progress: Float,
    val current: Int,
    val total: Int,
    val detail: String? = null
)

object VitaArchiveRepacker {
    private const val TAG = "VitaArchiveRepacker"
    private const val CACHE_DIR_NAME = "install_repack_cache"

    fun canRepack(path: String): Boolean {
        return InstallerArchiveSafety.detectContainerFormat(File(path)) == InstallerArchiveFormat.ZIP
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

                val writtenNames = LinkedHashMap<String, ZipArchiveEntry>()
                ZipOutputStream(output.outputStream().buffered()).use { zipOut ->
                    zipOut.setLevel(Deflater.DEFAULT_COMPRESSION)

                    entries.forEachIndexed { index, entry ->
                        val normalizedName = normalizeArchiveEntryName(entry.name)
                            ?: throw IOException("Unsafe archive entry: ${entry.name}")
                        val identity = normalizedName.trimEnd('/').lowercase(Locale.US)
                        val previous = writtenNames.putIfAbsent(identity, entry)
                        if (previous != null) {
                            if (!identicalVitaminRuntime(inputZip, previous, entry))
                                throw IOException("Ambiguous duplicate archive entry: $normalizedName")
                            reportProgress(index + 1, entries.size, normalizedName, onProgress)
                            return@forEachIndexed
                        }
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

    /** Packs a native-expanded Vita content directory to the ZIP accepted by the existing installer. */
    internal fun repackDirectoryToInstallZip(
        contentRoot: File,
        outputRoot: File,
        maxBytes: Long,
        freeSpaceReserveBytes: Long = 0L,
        isCancelled: () -> Boolean = { false },
        onProgress: (EmbeddedPackageProgress) -> Unit = {}
    ): File? {
        if (!contentRoot.isDirectory || Files.isSymbolicLink(contentRoot.toPath()) || maxBytes <= 0L) return null
        var partial: File? = null
        var output: File? = null
        return runCatching {
            val root = contentRoot.canonicalFile.toPath()
            val files = mutableListOf<Path>()
            var totalBytes = 0L
            Files.walkFileTree(root, object : SimpleFileVisitor<Path>() {
                override fun preVisitDirectory(dir: Path, attrs: BasicFileAttributes): FileVisitResult {
                    if (attrs.isSymbolicLink || Files.isSymbolicLink(dir)) throw IOException("Directory contains a symbolic link")
                    if (!dir.toRealPath(LinkOption.NOFOLLOW_LINKS).startsWith(root)) throw IOException("Directory escaped the selected game root")
                    return FileVisitResult.CONTINUE
                }

                override fun visitFile(file: Path, attrs: BasicFileAttributes): FileVisitResult {
                    if (!attrs.isRegularFile || attrs.isSymbolicLink || Files.isSymbolicLink(file)) throw IOException("Game content contains a non-regular file")
                    val relative = root.relativize(file).toString().replace('\\', '/')
                    if (normalizeArchiveEntryName(relative) == null) throw IOException("Unsafe extracted game path")
                    val size = attrs.size()
                    if (size < 0L || size > maxBytes - totalBytes) throw IOException("Extracted game exceeds byte limit")
                    totalBytes += size
                    files.add(file)
                    return FileVisitResult.CONTINUE
                }
            })
            if (files.isEmpty()) throw IOException("Extracted game content is empty")
            if (totalBytes > Long.MAX_VALUE - freeSpaceReserveBytes ||
                outputRoot.usableSpace < totalBytes + freeSpaceReserveBytes) {
                throw IOException("Not enough free space to repack extracted game content")
            }
            files.sortBy { root.relativize(it).toString().replace('\\', '/') }
            val target = File(outputRoot, "expanded-game-${UUID.randomUUID()}.zip")
            val temporary = File(outputRoot, ".${target.name}.partial")
            output = target
            partial = temporary
            val seen = mutableSetOf<String>()
            var copied = 0L
            ZipOutputStream(temporary.outputStream().buffered()).use { zipOut ->
                zipOut.setLevel(Deflater.DEFAULT_COMPRESSION)
                files.forEach { file ->
                    checkArchiveCancelled(isCancelled)
                    val relative = root.relativize(file).toString().replace('\\', '/')
                    val normalized = normalizeArchiveEntryName(relative) ?: throw IOException("Unsafe extracted game path")
                    if (!seen.add(normalized.lowercase(Locale.ROOT))) throw IOException("Duplicate extracted game path")
                    val entry = ZipEntry(normalized).apply { method = ZipEntry.DEFLATED }
                    zipOut.putNextEntry(entry)
                    Files.newInputStream(file).use { input ->
                        val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                        while (true) {
                            checkArchiveCancelled(isCancelled)
                            val count = input.read(buffer)
                            if (count < 0) break
                            if (count == 0) continue
                            copied += count
                            if (copied > maxBytes) throw IOException("Extracted game exceeds byte limit")
                            zipOut.write(buffer, 0, count)
                            onProgress(EmbeddedPackageProgress(copied, totalBytes, normalized))
                        }
                    }
                    zipOut.closeEntry()
                }
            }
            if (isCancelled() || Thread.currentThread().isInterrupted) throw IOException("Archive preparation was cancelled")
            try {
                Files.move(temporary.toPath(), target.toPath(), java.nio.file.StandardCopyOption.ATOMIC_MOVE)
            } catch (_: java.nio.file.AtomicMoveNotSupportedException) {
                Files.move(temporary.toPath(), target.toPath())
            }
            target
        }.onFailure {
            partial?.delete()
            output?.delete()
        }.getOrNull()
    }

    private fun checkArchiveCancelled(isCancelled: () -> Boolean) {
        if (isCancelled() || Thread.currentThread().isInterrupted) throw IOException("Archive preparation was cancelled")
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

    private fun identicalVitaminRuntime(zip: ZipFile, first: ZipArchiveEntry, second: ZipArchiveEntry): Boolean {
        val name = normalizeArchiveEntryName(first.name) ?: return false
        val folded = name.lowercase(Locale.US)
        if (name != normalizeArchiveEntryName(second.name) || first.isDirectory || second.isDirectory ||
            (folded != "sce_module/steroid.suprx" && !folded.endsWith("/sce_module/steroid.suprx")) ||
            first.size !in 1L..(4L * 1024 * 1024) || first.size != second.size || first.crc != second.crc ||
            !zip.canReadEntryData(first) || !zip.canReadEntryData(second)) return false

        fun readVerified(entry: ZipArchiveEntry): ByteArray? = zip.getInputStream(entry).use { input ->
            val bytes = ByteArray(entry.size.toInt())
            var offset = 0
            while (offset < bytes.size) {
                val count = input.read(bytes, offset, bytes.size - offset)
                if (count < 0) return null
                if (count == 0) continue
                offset += count
            }
            if (input.read() != -1 || CRC32().apply { update(bytes) }.value != entry.crc) return null
            bytes
        }
        val original = readVerified(first) ?: return false
        val repeated = readVerified(second) ?: return false
        return original.contentEquals(repeated)
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
