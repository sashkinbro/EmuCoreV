package com.sbro.emucorev.data

import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.InputStream
import java.nio.file.AtomicMoveNotSupportedException
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.nio.file.LinkOption

/** Reads only the owned ECV cache and supplied bundled data; it has no network or Vita3K fallback. */
class EcvCompatibilityDatasetRepository(
    private val cacheFile: File,
    private val bundledSource: () -> InputStream,
    private val parser: EcvCompatibilityParser = EcvCompatibilityParser()
) {
    private val lock = Any()

    fun load(): EcvCompatibilityDataset = synchronized(lock) {
        val cached = if (isSafeCachePath() && cacheFile.isFile) {
            runCatching { parser.parse(readBounded(FileInputStream(cacheFile))) }.getOrNull()
        } else {
            null
        }
        cached ?: loadBundled()
    }

    /** Validates all bytes before writing, then atomically swaps a synced temporary file into place. */
    fun replaceCache(source: InputStream): EcvCompatibilityDataset = synchronized(lock) {
        val bytes = readBounded(source)
        val dataset = parser.parse(bytes)
        require(isSafeCachePath()) { "ECV compatibility cache path must not contain symbolic links" }
        val parent = cacheFile.absoluteFile.parentFile
            ?: throw IllegalStateException("ECV compatibility cache must have a parent directory")
        if (!parent.isDirectory && !parent.mkdirs() && !parent.isDirectory) {
            throw IllegalStateException("Could not create the ECV compatibility cache directory")
        }
        require(isSafeCachePath()) { "ECV compatibility cache path must not contain symbolic links" }

        val temporary = File.createTempFile("ecv-compatibility-", ".tmp", parent)
        try {
            FileOutputStream(temporary).use { output ->
                output.write(bytes)
                output.fd.sync()
            }
            try {
                Files.move(
                    temporary.toPath(),
                    cacheFile.toPath(),
                    StandardCopyOption.ATOMIC_MOVE,
                    StandardCopyOption.REPLACE_EXISTING
                )
            } catch (error: AtomicMoveNotSupportedException) {
                throw IllegalStateException("Atomic ECV compatibility cache replacement is unavailable", error)
            }
        } finally {
            if (temporary.exists()) temporary.delete()
        }
        dataset
    }

    private fun loadBundled(): EcvCompatibilityDataset {
        return runCatching { parser.parse(readBounded(bundledSource())) }
            .getOrElse { EcvCompatibilityDataset.EMPTY }
    }

    private fun readBounded(source: InputStream): ByteArray {
        source.use { input ->
            val output = ByteArrayOutputStream()
            val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
            var total = 0
            while (true) {
                val remaining = EcvCompatibilityParser.MAX_DOCUMENT_BYTES - total
                val requested = minOf(buffer.size, remaining + 1)
                var count = input.read(buffer, 0, requested)
                if (count < 0) break
                if (count == 0) {
                    val next = input.read()
                    if (next < 0) break
                    buffer[0] = next.toByte()
                    count = 1
                }
                total += count
                require(total <= EcvCompatibilityParser.MAX_DOCUMENT_BYTES) {
                    "ECV compatibility document exceeds the size limit"
                }
                output.write(buffer, 0, count)
            }
            return output.toByteArray()
        }
    }

    private fun isSafeCachePath(): Boolean {
        var path = cacheFile.absoluteFile.toPath()
        while (true) {
            if (Files.exists(path, LinkOption.NOFOLLOW_LINKS) && Files.isSymbolicLink(path)) return false
            path = path.parent ?: return true
        }
    }
}
