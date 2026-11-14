package com.sbro.emucorev.data

import java.io.File
import java.io.FileOutputStream
import java.util.UUID
import java.util.zip.CRC32
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/** Bounded, transactional installation and discovery of complete RetroArch Slang packs. */
internal class RetroArchShaderPackStore(
    private val root: File,
    private val cache: File,
    private val lock: Mutex = installationLock
) {
    companion object {
        private val installationLock = Mutex()
        private const val OFFICIAL_MARKER = ".official-pack"
        private const val REMOVED_MARKER = ".removed-presets"
        private const val MAX_ARCHIVE_BYTES = 256L * 1024 * 1024
        private const val MAX_EXPANDED_BYTES = 768L * 1024 * 1024
        private const val MAX_ENTRIES = 20_000
    }

    suspend fun listPresets(): List<RetroArchShaderPreset> = lock.withLock {
        recoverInterruptedInstall()
        listPresetsUnlocked()
    }

    suspend fun hasInstalledPack(): Boolean = lock.withLock {
        recoverInterruptedInstall()
        root.resolve(OFFICIAL_MARKER).isFile && listPresetsUnlocked().isNotEmpty()
    }

    suspend fun installArchive(
        archive: File,
        official: Boolean,
        onExtractionChunk: () -> Unit = {}
    ): Int {
        lock.lock()
        try {
            cache.mkdirs()
            cache.listFiles().orEmpty()
                .filter { it.isDirectory && it.name.startsWith("shader-stage-") }
                .forEach { stale -> check(stale.deleteRecursively()) { "Unable to clear an abandoned shader install" } }
            val staging = File(cache, "shader-stage-${UUID.randomUUID()}")
            val backup = File(root.parentFile, "${root.name}.backup")
            var staleBackup: File? = null
            check(staging.mkdirs()) { "Unable to stage shader pack" }
            try {
                extract(archive, staging, onExtractionChunk)
                val presets = RetroArchShaderPresetParser.inspectPresets(staging)
                require(presets.isNotEmpty()) { "No structurally valid .slangp presets in archive" }
                if (official) staging.resolve(OFFICIAL_MARKER).writeText("1\n")
                currentCoroutineContext().ensureActive()
                recoverInterruptedInstall()
                if (backup.exists()) {
                    staleBackup = File(root.parentFile, "${root.name}.stale-${UUID.randomUUID()}")
                    check(backup.renameTo(staleBackup)) { "Unable to retire prior shader backup" }
                }
                if (root.exists()) check(root.renameTo(backup)) { "Unable to stage current shader pack" }
                if (!staging.renameTo(root)) {
                    if (backup.exists()) check(backup.renameTo(root)) { "Unable to restore current shader pack" }
                    error("Unable to install shader pack")
                }
                staleBackup?.deleteRecursively()
                return presets.size
            } finally {
                staging.deleteRecursively()
            }
        } finally {
            lock.unlock()
        }
    }

    suspend fun removePresets(paths: Set<String>): Result<Int> = try {
        Result.success(lock.withLock {
            recoverInterruptedInstall()
            val available = listPresetsUnlocked().map { it.absolutePath }.toSet()
            require(paths.isNotEmpty() && paths.all { it in available }) { "Unknown shader preset" }
            val marker = root.resolve(REMOVED_MARKER)
            val removed = marker.takeIf(File::isFile)?.readLines()?.toMutableSet() ?: mutableSetOf()
            paths.forEach { path ->
                val file = File(path).canonicalFile
                require(isInside(root.canonicalFile, file)) { "Shader preset is outside the pack" }
                removed += file.relativeTo(root.canonicalFile).invariantSeparatorsPath
            }
            val temporary = root.resolve(".removed-${UUID.randomUUID()}")
            try {
                FileOutputStream(temporary).use { output ->
                    output.write(removed.sorted().joinToString("\n", postfix = "\n").toByteArray())
                    output.fd.sync()
                }
                try {
                    java.nio.file.Files.move(
                        temporary.toPath(), marker.toPath(),
                        java.nio.file.StandardCopyOption.ATOMIC_MOVE,
                        java.nio.file.StandardCopyOption.REPLACE_EXISTING
                    )
                } catch (_: java.nio.file.AtomicMoveNotSupportedException) {
                    java.nio.file.Files.move(
                        temporary.toPath(), marker.toPath(),
                        java.nio.file.StandardCopyOption.REPLACE_EXISTING
                    )
                }
            } finally {
                temporary.delete()
            }
            paths.size
        })
    } catch (cancelled: CancellationException) {
        throw cancelled
    } catch (error: Exception) {
        Result.failure(error)
    }

    private fun listPresetsUnlocked(): List<RetroArchShaderPreset> {
        val removed = root.resolve(REMOVED_MARKER).takeIf(File::isFile)?.readLines()?.toSet().orEmpty()
        val base = root.canonicalFile
        return RetroArchShaderPresetParser.inspectPresets(base).mapNotNull { (file, inspected) ->
            val relative = file.relativeTo(base).invariantSeparatorsPath
            if (relative in removed) return@mapNotNull null
            RetroArchShaderPreset(
                label = relative.substringBeforeLast('.'),
                absolutePath = file.absolutePath,
                passCount = inspected.passCount,
                textureIds = inspected.textureIds,
                parameters = inspected.parameters
            )
        }.sortedBy { it.label.lowercase() }
    }

    private fun recoverInterruptedInstall() {
        val backup = File(root.parentFile, "${root.name}.backup")
        if (!root.exists() && backup.isDirectory) check(backup.renameTo(root)) { "Unable to restore shader backup" }
    }

    private suspend fun extract(archive: File, destination: File, onExtractionChunk: () -> Unit) {
        ZipFile(archive).use { zip ->
            require(archive.isFile && archive.length() <= MAX_ARCHIVE_BYTES) { "Shader archive is too large" }
            val entries = mutableListOf<ZipEntry>()
            val enumeration = zip.entries()
            var entryCount = 0
            while (enumeration.hasMoreElements()) {
                currentCoroutineContext().ensureActive()
                val entry = enumeration.nextElement()
                require(++entryCount <= MAX_ENTRIES) { "Too many files in shader archive" }
                if (entry.name.startsWith("__MACOSX/") || entry.name.substringAfterLast('/').startsWith('.'))
                    continue
                entries += entry
            }
            entries.forEach { entry -> validateEntryName(entry.name) }
            val commonRoot = commonArchiveRoot(entries.map { it.name })
            val seen = mutableSetOf<String>()
            var expandedBytes = 0L
            for (entry in entries) {
                currentCoroutineContext().ensureActive()
                val relative = entry.name.removePrefix(commonRoot).trimEnd('/')
                if (relative.isBlank()) continue
                require(seen.add(relative)) { "Shader archive has duplicate paths" }
                val output = File(destination, relative)
                require(isInside(destination.canonicalFile, output.canonicalFile)) { "Unsafe shader archive path" }
                if (entry.isDirectory) {
                    require(output.mkdirs() || output.isDirectory) { "Unable to create shader directory" }
                    continue
                }
                require(entry.size < 0 || entry.size <= MAX_EXPANDED_BYTES) { "Shader file is too large" }
                val parent = output.parentFile
                require(parent == null || parent.mkdirs() || parent.isDirectory) { "Unable to create shader directory" }
                val checksum = CRC32()
                var fileBytes = 0L
                zip.getInputStream(entry).use { input ->
                    FileOutputStream(output).use { sink ->
                        val buffer = ByteArray(64 * 1024)
                        while (true) {
                            currentCoroutineContext().ensureActive()
                            val count = input.read(buffer)
                            if (count < 0) break
                            fileBytes += count
                            expandedBytes += count
                            require(expandedBytes <= MAX_EXPANDED_BYTES) { "Expanded shader pack is too large" }
                            checksum.update(buffer, 0, count)
                            sink.write(buffer, 0, count)
                            onExtractionChunk()
                        }
                        sink.fd.sync()
                    }
                }
                require((entry.size < 0 || fileBytes == entry.size) && (entry.crc < 0 || checksum.value == entry.crc)) {
                    "Shader archive is incomplete or corrupt"
                }
            }
        }
    }

    private fun validateEntryName(name: String) {
        require(name.isNotBlank() && !name.startsWith('/') && !name.startsWith('\\') && '\\' !in name && ':' !in name) {
            "Unsafe shader archive path"
        }
        val path = name.removeSuffix("/")
        require(path.isNotEmpty() && path.split('/').none { it == ".." || it == "." || it.isEmpty() }) {
            "Unsafe shader archive path"
        }
    }

    private fun commonArchiveRoot(paths: List<String>): String {
        if (paths.isEmpty()) return ""
        val first = paths.first().substringBefore('/', missingDelimiterValue = "")
        if (first.isBlank()) return ""
        return if (paths.all { it == first || it.startsWith("$first/") }) "$first/" else ""
    }

    private fun isInside(root: File, file: File): Boolean =
        file.canonicalPath.startsWith(root.canonicalPath.trimEnd(File.separatorChar) + File.separator)
}
