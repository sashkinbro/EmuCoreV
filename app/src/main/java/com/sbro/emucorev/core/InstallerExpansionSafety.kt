package com.sbro.emucorev.core

import java.io.Closeable
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.OutputStream
import java.nio.file.FileVisitResult
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.Path
import java.nio.file.SimpleFileVisitor
import java.nio.file.attribute.BasicFileAttributes
import java.util.Locale
import java.util.UUID

internal enum class InstallerArchiveFormat { ZIP, SEVEN_Z, RAR4, RAR5 }

internal enum class InstallerEntryKind { FILE, DIRECTORY, SYMBOLIC_LINK, HARD_LINK, OTHER }

internal enum class InstallerPackageKind { VPK, PKG, CONTENT_DIRECTORY }

internal enum class InstallerExpansionFailure {
    UNSUPPORTED_PATH,
    LINK_ENTRY,
    DUPLICATE_ENTRY,
    DUPLICATE_ARCHIVE,
    ARCHIVE_NOT_OPENED,
    MALFORMED_PACKAGE,
    DEPTH_LIMIT,
    ARCHIVE_COUNT_LIMIT,
    ENTRY_COUNT_LIMIT,
    EXPANDED_BYTES_LIMIT,
    DECODER_MEMORY_LIMIT,
    INSUFFICIENT_SPACE,
    CANCELLED,
    STAGING_IO
}

internal class InstallerExpansionException(
    val failure: InstallerExpansionFailure,
    message: String,
    cause: Throwable? = null
) : IOException(message, cause)

internal data class InstallerExpansionLimits(
    val maxNestedDepth: Int = 2,
    val maxArchives: Int = 8,
    val maxEntries: Int = 50_000,
    val maxExpandedBytes: Long = 32L * 1024 * 1024 * 1024,
    val maxDecoderMemoryBytes: Long = 256L * 1024 * 1024,
    val freeSpaceReserveBytes: Long = 256L * 1024 * 1024
) {
    init {
        require(maxNestedDepth >= 0)
        require(maxArchives > 0)
        require(maxEntries > 0)
        require(maxExpandedBytes > 0)
        require(maxDecoderMemoryBytes > 0)
        require(freeSpaceReserveBytes >= 0)
    }
}

internal data class InstallerArchiveMember(
    val path: String,
    val declaredSize: Long,
    val kind: InstallerEntryKind
)

internal data class InstallerPackageCandidate(
    val path: String,
    val kind: InstallerPackageKind,
    val declaredSize: Long,
    val displayName: String = path
)

internal sealed interface InstallerPackageSelection {
    data object None : InstallerPackageSelection
    data class Ready(val candidate: InstallerPackageCandidate) : InstallerPackageSelection
    data class RequiresChoice(val candidates: List<InstallerPackageCandidate>) : InstallerPackageSelection
}

internal object InstallerArchiveSafety {
    fun hasVitaPkgSignature(header: ByteArray): Boolean =
        header.size >= 4 && header[0] == 0x7f.toByte() && header[1] == 0x50.toByte() &&
            header[2] == 0x4b.toByte() && header[3] == 0x47.toByte()

    fun detectContainerFormat(header: ByteArray): InstallerArchiveFormat? {
        fun startsWith(vararg bytes: Int): Boolean =
            header.size >= bytes.size && bytes.indices.all { (header[it].toInt() and 0xff) == bytes[it] }

        return when {
            startsWith(0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c) -> InstallerArchiveFormat.SEVEN_Z
            startsWith(0x52, 0x61, 0x72, 0x21, 0x1a, 0x07, 0x00) -> InstallerArchiveFormat.RAR4
            startsWith(0x52, 0x61, 0x72, 0x21, 0x1a, 0x07, 0x01, 0x00) -> InstallerArchiveFormat.RAR5
            startsWith(0x50, 0x4b, 0x03, 0x04) ||
                startsWith(0x50, 0x4b, 0x05, 0x06) ||
                startsWith(0x50, 0x4b, 0x07, 0x08) -> InstallerArchiveFormat.ZIP
            else -> null
        }
    }

    fun detectContainerFormat(file: File): InstallerArchiveFormat? {
        if (!file.isFile || !file.canRead()) return null
        return runCatching {
            file.inputStream().buffered().use { input ->
                val header = ByteArray(8)
                var offset = 0
                while (offset < header.size) {
                    val count = input.read(header, offset, header.size - offset)
                    if (count < 0) break
                    if (count > 0) offset += count
                }
                detectContainerFormat(header.copyOf(offset))
            }
        }.getOrNull()
    }

    fun normalizeEntryPath(path: String): String? {
        if (path.isBlank() || path.indexOf('\u0000') >= 0) return null
        val slashPath = path.replace('\\', '/')
        if (slashPath.startsWith('/') || slashPath.startsWith("//") ||
            Regex("^[A-Za-z]:").containsMatchIn(slashPath) || slashPath.contains(':')) return null

        val components = slashPath.split('/')
        if (components.any { it == ".." }) return null
        val safe = components.filter { it.isNotEmpty() && it != "." }
        if (safe.isEmpty()) return null
        return safe.joinToString("/")
    }

    fun selectPackage(candidates: List<InstallerPackageCandidate>): InstallerPackageSelection {
        if (candidates.isEmpty()) return InstallerPackageSelection.None
        val normalized = candidates.map { candidate ->
            if (candidate.declaredSize < 0L) {
                throw InstallerExpansionException(
                    InstallerExpansionFailure.EXPANDED_BYTES_LIMIT,
                    "Package candidate has an unknown size"
                )
            }
            val safePath = normalizeEntryPath(candidate.path)
                ?: throw InstallerExpansionException(
                    InstallerExpansionFailure.UNSUPPORTED_PATH,
                    "Unsafe package candidate path"
                )
            candidate.copy(path = safePath)
        }
        if (normalized.size == 1) return InstallerPackageSelection.Ready(normalized.single())
        val ordered = normalized.sortedWith(
            compareBy<InstallerPackageCandidate> { it.path.lowercase(Locale.ROOT) }
                .thenBy { it.path }
                .thenBy { it.kind.name }
        )
        return InstallerPackageSelection.RequiresChoice(ordered)
    }
}

internal class InstallerExpansionBudget(
    private val limits: InstallerExpansionLimits,
    private val stagingRoot: File,
    private val isCancelled: () -> Boolean = { false }
) {
    private val archiveIds = mutableSetOf<String>()
    private val entryIdentities = mutableSetOf<Pair<String, String>>()
    private var entryCount = 0
    private var declaredBytes = 0L
    private var actualBytes = 0L

    @Synchronized
    fun enterArchive(archiveId: String = UUID.randomUUID().toString(), depth: Int) {
        checkCancellation()
        if (depth !in 0..limits.maxNestedDepth) {
            fail(InstallerExpansionFailure.DEPTH_LIMIT, "Nested archive depth limit exceeded")
        }
        if (!archiveIds.add(archiveId)) {
            fail(InstallerExpansionFailure.DUPLICATE_ARCHIVE, "Archive layer was registered twice")
        }
        if (archiveIds.size > limits.maxArchives) {
            fail(InstallerExpansionFailure.ARCHIVE_COUNT_LIMIT, "Nested archive count limit exceeded")
        }
    }

    @Synchronized
    fun registerEntry(archiveId: String, member: InstallerArchiveMember) {
        checkCancellation()
        if (archiveId !in archiveIds) {
            fail(InstallerExpansionFailure.ARCHIVE_NOT_OPENED, "Archive layer was not registered")
        }
        val safePath = InstallerArchiveSafety.normalizeEntryPath(member.path)
            ?: fail(InstallerExpansionFailure.UNSUPPORTED_PATH, "Archive entry path is unsafe")
        if (member.kind == InstallerEntryKind.SYMBOLIC_LINK || member.kind == InstallerEntryKind.HARD_LINK) {
            fail(InstallerExpansionFailure.LINK_ENTRY, "Archive links are not installable entries")
        }
        if (member.kind == InstallerEntryKind.OTHER) {
            fail(InstallerExpansionFailure.LINK_ENTRY, "Unsupported special archive entry")
        }
        if (member.declaredSize < 0L) {
            fail(InstallerExpansionFailure.EXPANDED_BYTES_LIMIT, "Archive entry has an unknown size")
        }

        val identity = archiveId to safePath.trimEnd('/').lowercase(Locale.ROOT)
        if (!entryIdentities.add(identity)) {
            fail(InstallerExpansionFailure.DUPLICATE_ENTRY, "Archive contains duplicate entry identities")
        }
        entryCount++
        if (entryCount > limits.maxEntries) {
            fail(InstallerExpansionFailure.ENTRY_COUNT_LIMIT, "Archive entry count limit exceeded")
        }
        declaredBytes = addBounded(declaredBytes, member.declaredSize)
        if (declaredBytes > limits.maxExpandedBytes) {
            fail(InstallerExpansionFailure.EXPANDED_BYTES_LIMIT, "Archive declared expansion exceeds limit")
        }
        verifyFreeSpace(declaredBytes)
    }

    @Synchronized
    fun recordActualBytes(count: Long) {
        checkCancellation()
        if (count < 0L) fail(InstallerExpansionFailure.EXPANDED_BYTES_LIMIT, "Invalid decoded byte count")
        actualBytes = addBounded(actualBytes, count)
        if (actualBytes > limits.maxExpandedBytes) {
            fail(InstallerExpansionFailure.EXPANDED_BYTES_LIMIT, "Archive decoded bytes exceed limit")
        }
        verifyFreeSpace(declaredBytes)
    }

    fun requireDecoderMemory(requiredBytes: Long) {
        checkCancellation()
        if (requiredBytes < 0L || requiredBytes > limits.maxDecoderMemoryBytes) {
            fail(InstallerExpansionFailure.DECODER_MEMORY_LIMIT, "Archive decoder memory exceeds limit")
        }
    }

    private fun verifyFreeSpace(requiredExpandedBytes: Long) {
        val remainingExpansion = (requiredExpandedBytes - actualBytes).coerceAtLeast(0L)
        val withReserve = addBounded(remainingExpansion, limits.freeSpaceReserveBytes)
        if (stagingRoot.usableSpace < withReserve) {
            fail(InstallerExpansionFailure.INSUFFICIENT_SPACE, "Not enough free space for archive staging")
        }
    }

    private fun checkCancellation() {
        if (Thread.currentThread().isInterrupted || isCancelled()) {
            fail(InstallerExpansionFailure.CANCELLED, "Archive expansion was cancelled")
        }
    }

    private fun addBounded(current: Long, increment: Long): Long =
        if (increment > Long.MAX_VALUE - current) Long.MAX_VALUE else current + increment

    private fun fail(reason: InstallerExpansionFailure, message: String): Nothing =
        throw InstallerExpansionException(reason, message)
}

internal class InstallerExpansionSession private constructor(
    private val parent: File,
    val root: File
) : Closeable {
    private var closed = false

    fun createAtomicOutput(relativePath: String): InstallerAtomicOutput {
        check(!closed) { "Expansion session is closed" }
        val safePath = InstallerArchiveSafety.normalizeEntryPath(relativePath)
            ?: throw InstallerExpansionException(InstallerExpansionFailure.UNSUPPORTED_PATH, "Unsafe staged output path")
        val target = File(root, safePath)
        val parentDirectory = target.parentFile ?: root
        if (!parentDirectory.mkdirs() && !parentDirectory.isDirectory) {
            throw InstallerExpansionException(InstallerExpansionFailure.STAGING_IO, "Could not create staging directory")
        }
        if (!isWithinSession(parentDirectory)) {
            throw InstallerExpansionException(InstallerExpansionFailure.UNSUPPORTED_PATH, "Staged output escaped its session")
        }
        if (target.exists() || Files.isSymbolicLink(target.toPath())) {
            throw InstallerExpansionException(InstallerExpansionFailure.DUPLICATE_ENTRY, "Staged output already exists")
        }
        val partial = File(parentDirectory, ".${target.name}.${UUID.randomUUID()}.partial")
        return try {
            InstallerAtomicOutput(partial, target, FileOutputStream(partial))
        } catch (error: IOException) {
            partial.delete()
            throw InstallerExpansionException(InstallerExpansionFailure.STAGING_IO, "Could not create staged output", error)
        }
    }

    private fun isWithinSession(file: File): Boolean {
        val rootPath = root.canonicalFile.toPath()
        val filePath = file.canonicalFile.toPath()
        return filePath.startsWith(rootPath) && !Files.isSymbolicLink(root.toPath())
    }

    override fun close() {
        if (closed) return
        closed = true
        if (Files.isSymbolicLink(root.toPath()) || root.canonicalFile.parentFile != parent.canonicalFile) return
        deleteTreeWithoutFollowingLinks(root.toPath())
    }

    private fun deleteTreeWithoutFollowingLinks(path: Path) {
        if (!Files.exists(path, LinkOption.NOFOLLOW_LINKS)) return
        Files.walkFileTree(path, object : SimpleFileVisitor<Path>() {
            override fun visitFile(file: Path, attrs: BasicFileAttributes): FileVisitResult {
                Files.deleteIfExists(file)
                return FileVisitResult.CONTINUE
            }

            override fun postVisitDirectory(dir: Path, error: IOException?): FileVisitResult {
                if (error != null) throw error
                Files.deleteIfExists(dir)
                return FileVisitResult.CONTINUE
            }
        })
    }

    companion object {
        fun create(cacheRoot: File): InstallerExpansionSession {
            if (!cacheRoot.exists() && !cacheRoot.mkdirs()) {
                throw InstallerExpansionException(InstallerExpansionFailure.STAGING_IO, "Could not create staging root")
            }
            if (!cacheRoot.isDirectory || Files.isSymbolicLink(cacheRoot.toPath())) {
                throw InstallerExpansionException(InstallerExpansionFailure.STAGING_IO, "Staging root is not a safe directory")
            }
            val root = File(cacheRoot, "installer-expansion-${UUID.randomUUID()}")
            if (!root.mkdir()) {
                throw InstallerExpansionException(InstallerExpansionFailure.STAGING_IO, "Could not create isolated staging session")
            }
            return InstallerExpansionSession(cacheRoot.canonicalFile, root)
        }
    }
}

internal class InstallerAtomicOutput internal constructor(
    private val partial: File,
    private val target: File,
    output: OutputStream
) : Closeable {
    private var finished = false
    val stream: OutputStream = output

    @Synchronized
    fun commit(cancelled: () -> Boolean = { false }): Boolean {
        if (finished) throw IOException("Staged output is already closed")
        if (cancelled() || Thread.currentThread().isInterrupted) {
            discard()
            return false
        }
        try {
            stream.flush()
            stream.close()
            if (target.exists() || !partial.renameTo(target)) {
                throw InstallerExpansionException(InstallerExpansionFailure.STAGING_IO, "Could not atomically promote staged output")
            }
            finished = true
            return true
        } catch (error: Throwable) {
            discard()
            throw error
        }
    }

    @Synchronized
    private fun discard() {
        if (finished) return
        runCatching { stream.close() }
        partial.delete()
        finished = true
    }

    override fun close() = discard()
}
