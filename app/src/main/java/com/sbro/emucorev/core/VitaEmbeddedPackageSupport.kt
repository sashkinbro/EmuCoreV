package com.sbro.emucorev.core

import org.apache.commons.compress.archivers.zip.ZipArchiveEntry
import org.apache.commons.compress.archivers.zip.ZipArchiveInputStream
import org.apache.commons.compress.archivers.zip.ZipFile
import java.io.Closeable
import java.io.File
import java.io.FilterInputStream
import java.io.IOException
import java.io.InputStream
import java.io.PushbackInputStream
import java.util.Locale
import java.util.zip.CRC32

internal data class EmbeddedPackageProgress(
    val copiedBytes: Long,
    val totalBytes: Long,
    val archiveEntryPath: String
)

internal class PreparedEmbeddedPackage internal constructor(
    val candidate: InstallerPackageCandidate,
    val file: File,
    private val session: InstallerExpansionSession
) : Closeable {
    override fun close() = session.close()
}

/** Finds package payloads in ZIP wrappers and stages only the explicitly selected payload. */
internal object VitaEmbeddedPackageSupport {
    private const val OUTER_LAYER = "outer"
    private const val SFO_PATH = "sce_sys/param.sfo"
    private const val MAX_SFO_BYTES = 1024L * 1024L

    fun scanZip(
        archivePath: String,
        stagingRoot: File,
        limits: InstallerExpansionLimits = InstallerExpansionLimits(),
        isCancelled: () -> Boolean = { false }
    ): InstallerPackageSelection {
        val source = File(archivePath)
        if (InstallerArchiveSafety.detectContainerFormat(source) != InstallerArchiveFormat.ZIP) {
            return InstallerPackageSelection.None
        }
        val budget = InstallerExpansionBudget(limits, stagingRoot, isCancelled)
        val candidates = mutableListOf<InstallerPackageCandidate>()
        val identities = mutableSetOf<String>()

        ZipFile.builder().setFile(source).get().use { outer ->
            budget.enterArchive(OUTER_LAYER, depth = 0)
            outer.entries.asSequence().forEachIndexed { index, entry ->
                ensureNotCancelled(isCancelled)
                val kind = entryKind(entry)
                val path = InstallerArchiveSafety.normalizeEntryPath(entry.name)
                    ?: if (entry.isDirectory && isRootDirectoryMarker(entry.name)) return@forEachIndexed
                    else throw InstallerExpansionException(
                        InstallerExpansionFailure.UNSUPPORTED_PATH,
                        "Outer ZIP contains an unsafe member path"
                    )
                budget.registerEntry(OUTER_LAYER, InstallerArchiveMember(path, entry.size, kind))
                val identity = path.trimEnd('/').lowercase(Locale.ROOT)
                if (!identities.add(identity)) {
                    throw InstallerExpansionException(
                        InstallerExpansionFailure.DUPLICATE_ENTRY,
                        "Outer ZIP contains duplicate member identities"
                    )
                }
                if (kind != InstallerEntryKind.FILE || entry.size == 0L) return@forEachIndexed

                outer.getInputStream(entry).use { payload ->
                    val metered = MeteredInputStream(payload, budget)
                    val prefixStream = PushbackInputStream(metered, 8)
                    val header = readPrefix(prefixStream, 8)
                    when {
                        InstallerArchiveSafety.hasVitaPkgSignature(header) -> {
                            candidates += InstallerPackageCandidate(path, InstallerPackageKind.PKG, entry.size)
                        }
                        InstallerArchiveSafety.detectContainerFormat(header) == InstallerArchiveFormat.ZIP -> {
                            prefixStream.unread(header)
                            val nestedId = "nested-$index"
                            budget.enterArchive(nestedId, depth = 1)
                            val validVpk = containsValidVitaSfo(prefixStream, nestedId, budget, limits, isCancelled)
                            if (validVpk) {
                                candidates += InstallerPackageCandidate(path, InstallerPackageKind.VPK, entry.size)
                            } else if (path.substringAfterLast('/').endsWith(".vpk", ignoreCase = true)) {
                                throw InstallerExpansionException(
                                    InstallerExpansionFailure.MALFORMED_PACKAGE,
                                    "Nested VPK is missing valid Vita install metadata"
                                )
                            }
                        }
                        else -> Unit
                    }
                }
            }
        }
        return InstallerArchiveSafety.selectPackage(candidates)
    }

    fun extractSelected(
        archivePath: String,
        candidate: InstallerPackageCandidate,
        stagingRoot: File,
        limits: InstallerExpansionLimits = InstallerExpansionLimits(),
        isCancelled: () -> Boolean = { false },
        onProgress: (EmbeddedPackageProgress) -> Unit = {}
    ): PreparedEmbeddedPackage {
        val source = File(archivePath)
        if (InstallerArchiveSafety.detectContainerFormat(source) != InstallerArchiveFormat.ZIP) {
            throw InstallerExpansionException(InstallerExpansionFailure.MALFORMED_PACKAGE, "Source is not a ZIP wrapper")
        }
        val requestedPath = InstallerArchiveSafety.normalizeEntryPath(candidate.path)
            ?: throw InstallerExpansionException(InstallerExpansionFailure.UNSUPPORTED_PATH, "Unsafe package member path")
        val session = InstallerExpansionSession.create(stagingRoot)
        try {
            val budget = InstallerExpansionBudget(limits, session.root, isCancelled)
            budget.enterArchive(OUTER_LAYER, depth = 0)
            var selected: ZipArchiveEntry? = null
            var foundIdentities = mutableSetOf<String>()
            ZipFile.builder().setFile(source).get().use { outer ->
                outer.entries.asSequence().forEach { entry ->
                    ensureNotCancelled(isCancelled)
                    val path = InstallerArchiveSafety.normalizeEntryPath(entry.name)
                        ?: if (entry.isDirectory && isRootDirectoryMarker(entry.name)) return@forEach
                        else throw InstallerExpansionException(
                            InstallerExpansionFailure.UNSUPPORTED_PATH,
                            "Outer ZIP contains an unsafe member path"
                        )
                    budget.registerEntry(OUTER_LAYER, InstallerArchiveMember(path, entry.size, entryKind(entry)))
                    if (!foundIdentities.add(path.trimEnd('/').lowercase(Locale.ROOT))) {
                        throw InstallerExpansionException(
                            InstallerExpansionFailure.DUPLICATE_ENTRY,
                            "Outer ZIP contains duplicate member identities"
                        )
                    }
                    if (path == requestedPath) selected = entry
                }

                val entry = selected
                    ?: throw InstallerExpansionException(InstallerExpansionFailure.MALFORMED_PACKAGE, "Selected package is no longer in the ZIP")
                if (entryKind(entry) != InstallerEntryKind.FILE || entry.size <= 0L) {
                    throw InstallerExpansionException(InstallerExpansionFailure.MALFORMED_PACKAGE, "Selected package is not a regular file")
                }
                val suffix = when (candidate.kind) {
                    InstallerPackageKind.VPK -> "vpk"
                    InstallerPackageKind.PKG -> "pkg"
                    InstallerPackageKind.CONTENT_DIRECTORY -> throw InstallerExpansionException(
                        InstallerExpansionFailure.MALFORMED_PACKAGE,
                        "Content directories cannot be extracted from a ZIP member"
                    )
                }
                val output = session.createAtomicOutput("embedded.$suffix")
                val crc = CRC32()
                var copied = 0L
                try {
                    if (!outer.canReadEntryData(entry)) {
                        throw InstallerExpansionException(
                            InstallerExpansionFailure.MALFORMED_PACKAGE,
                            "Selected package uses an unsupported outer ZIP method"
                        )
                    }
                    outer.getInputStream(entry).use { input ->
                        val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                        while (true) {
                            ensureNotCancelled(isCancelled)
                            val count = input.read(buffer)
                            if (count < 0) break
                            if (count == 0) continue
                            budget.recordActualBytes(count.toLong())
                            output.stream.write(buffer, 0, count)
                            crc.update(buffer, 0, count)
                            copied += count
                            onProgress(EmbeddedPackageProgress(copied, entry.size, requestedPath))
                        }
                    }
                    if (copied != entry.size || crc.value != entry.crc) {
                        throw InstallerExpansionException(
                            InstallerExpansionFailure.MALFORMED_PACKAGE,
                            "Selected package payload failed size or CRC verification"
                        )
                    }
                    if (!output.commit(isCancelled)) {
                        throw InstallerExpansionException(InstallerExpansionFailure.CANCELLED, "Package extraction was cancelled")
                    }
                } finally {
                    output.close()
                }
            }

            val prepared = File(session.root, "embedded.${if (candidate.kind == InstallerPackageKind.VPK) "vpk" else "pkg"}")
            when (candidate.kind) {
                InstallerPackageKind.VPK -> if (!isSafeInstallableVpk(prepared, session.root, limits, isCancelled)) {
                    throw InstallerExpansionException(
                        InstallerExpansionFailure.MALFORMED_PACKAGE,
                        "Nested VPK is missing valid Vita install metadata"
                    )
                }
                InstallerPackageKind.PKG -> if (!hasPkgSignature(prepared)) {
                    throw InstallerExpansionException(
                        InstallerExpansionFailure.MALFORMED_PACKAGE,
                        "Nested PKG signature changed during extraction"
                    )
                }
                InstallerPackageKind.CONTENT_DIRECTORY -> throw InstallerExpansionException(
                    InstallerExpansionFailure.MALFORMED_PACKAGE,
                    "Content directories cannot be extracted from a ZIP member"
                )
            }
            return PreparedEmbeddedPackage(candidate.copy(path = requestedPath), prepared, session)
        } catch (error: Throwable) {
            session.close()
            throw error
        }
    }

    @Suppress("DEPRECATION")
    private fun containsValidVitaSfo(
        input: InputStream,
        archiveId: String,
        budget: InstallerExpansionBudget,
        limits: InstallerExpansionLimits,
        isCancelled: () -> Boolean
    ): Boolean {
        ZipArchiveInputStream(input).use { nested ->
            var validSfoFound = false
            var count = 0
            val identities = mutableSetOf<String>()
            while (true) {
                ensureNotCancelled(isCancelled)
                val entry = nested.nextZipEntry ?: break
                count++
                if (count > limits.maxEntries) {
                    throw InstallerExpansionException(InstallerExpansionFailure.ENTRY_COUNT_LIMIT, "Nested VPK entry count limit exceeded")
                }
                val path = InstallerArchiveSafety.normalizeEntryPath(entry.name)
                    ?: if (entry.isDirectory && isRootDirectoryMarker(entry.name)) continue
                    else throw InstallerExpansionException(InstallerExpansionFailure.UNSUPPORTED_PATH, "Nested VPK contains an unsafe path: ${entry.name}")
                val kind = entryKind(entry)
                budget.registerEntry(archiveId, InstallerArchiveMember(path, entry.size.coerceAtLeast(0L), kind))
                if (!identities.add(path.trimEnd('/').lowercase(Locale.ROOT))) {
                    throw InstallerExpansionException(InstallerExpansionFailure.DUPLICATE_ENTRY, "Nested VPK contains duplicate paths")
                }
                if (kind != InstallerEntryKind.FILE ||
                    (path != SFO_PATH && !path.endsWith("/$SFO_PATH", ignoreCase = true))) continue
                if (entry.size > MAX_SFO_BYTES || !nested.canReadEntryData(entry)) continue

                val bytes = readCurrentEntryBounded(nested, MAX_SFO_BYTES.toInt()) ?: continue
                if (bytes.size < 20) continue
                if (entry.crc >= 0 && CRC32().apply { update(bytes) }.value != entry.crc) continue
                val metadata = VitaSfoParser.parse(bytes)
                if (metadata.titleId?.let { Regex("[A-Z]{4}[0-9]{5}").matches(it) } == true &&
                    !metadata.category.isNullOrBlank()) validSfoFound = true
            }
            return validSfoFound
        }
    }

    internal fun isSafeInstallableVpk(
        file: File,
        stagingRoot: File,
        limits: InstallerExpansionLimits,
        isCancelled: () -> Boolean
    ): Boolean {
        val budget = InstallerExpansionBudget(limits, stagingRoot, isCancelled)
        budget.enterArchive("selected-vpk", depth = 0)
        return file.inputStream().use { input ->
            containsValidVitaSfo(MeteredInputStream(input, budget), "selected-vpk", budget, limits, isCancelled)
        }
    }

    private class MeteredInputStream(
        input: InputStream,
        private val budget: InstallerExpansionBudget
    ) : FilterInputStream(input) {
        override fun read(): Int {
            val value = super.read()
            if (value >= 0) budget.recordActualBytes(1)
            return value
        }

        override fun read(buffer: ByteArray, offset: Int, length: Int): Int {
            val count = super.read(buffer, offset, length)
            if (count > 0) budget.recordActualBytes(count.toLong())
            return count
        }
    }

    private fun readPrefix(input: InputStream, limit: Int): ByteArray {
        val prefix = ByteArray(limit)
        var offset = 0
        while (offset < prefix.size) {
            val count = input.read(prefix, offset, prefix.size - offset)
            if (count < 0) break
            if (count > 0) offset += count
        }
        return prefix.copyOf(offset)
    }

    private fun readCurrentEntryBounded(input: InputStream, maximum: Int): ByteArray? {
        val output = java.io.ByteArrayOutputStream()
        val buffer = ByteArray(4096)
        while (true) {
            val count = input.read(buffer)
            if (count < 0) return output.toByteArray()
            if (count == 0) continue
            if (output.size() > maximum - count) return null
            output.write(buffer, 0, count)
        }
    }

    private fun entryKind(entry: ZipArchiveEntry): InstallerEntryKind {
        if (entry.isUnixSymlink) return InstallerEntryKind.SYMBOLIC_LINK
        if (entry.isDirectory) return InstallerEntryKind.DIRECTORY
        val type = entry.unixMode and 0xF000
        return if (type == 0 || type == 0x8000) InstallerEntryKind.FILE else InstallerEntryKind.OTHER
    }

    private fun isRootDirectoryMarker(name: String): Boolean =
        name.replace('\\', '/').split('/').all { it.isBlank() || it == "." }

    internal fun hasPkgSignature(file: File): Boolean = runCatching {
        file.inputStream().buffered().use { input ->
            val header = ByteArray(4)
            var offset = 0
            while (offset < header.size) {
                val count = input.read(header, offset, header.size - offset)
                if (count < 0) return@use false
                if (count > 0) offset += count
            }
            InstallerArchiveSafety.hasVitaPkgSignature(header)
        }
    }.getOrDefault(false)

    private fun ensureNotCancelled(isCancelled: () -> Boolean) {
        if (Thread.currentThread().isInterrupted || isCancelled()) {
            throw InstallerExpansionException(InstallerExpansionFailure.CANCELLED, "Package preparation was cancelled")
        }
    }

}
