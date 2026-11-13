package com.sbro.emucorev.core

import java.io.Closeable
import java.io.File
import java.nio.file.Files
import java.util.Locale

internal class VitaArchivePackagePlan internal constructor(
    val candidates: List<InstallerPackageCandidate>,
    private val session: InstallerExpansionSession,
    private val limits: InstallerExpansionLimits,
    private val maxAdditionalBytes: Long,
    private val isCancelled: () -> Boolean,
    private val onProgress: (EmbeddedPackageProgress) -> Unit
) : Closeable {
    fun prepare(candidate: InstallerPackageCandidate): PreparedEmbeddedPackage {
        if (candidate !in candidates) {
            throw InstallerExpansionException(InstallerExpansionFailure.MALFORMED_PACKAGE, "Package choice is not part of this archive")
        }
        checkCancelled()
        return when (candidate.kind) {
            InstallerPackageKind.VPK, InstallerPackageKind.PKG -> {
                val safePath = InstallerArchiveSafety.normalizeEntryPath(candidate.path)
                    ?: throw InstallerExpansionException(InstallerExpansionFailure.UNSUPPORTED_PATH, "Unsafe staged package path")
                val root = session.root.canonicalFile
                val file = File(root, safePath).canonicalFile
                if (!file.toPath().startsWith(root.toPath()) || !file.isFile || Files.isSymbolicLink(file.toPath())) {
                    throw InstallerExpansionException(InstallerExpansionFailure.UNSUPPORTED_PATH, "Selected package escaped private staging")
                }
                when (candidate.kind) {
                    InstallerPackageKind.VPK -> if (!VitaEmbeddedPackageSupport.isSafeInstallableVpk(
                            file,
                            session.root,
                            limits,
                            isCancelled
                        )) {
                        throw InstallerExpansionException(InstallerExpansionFailure.MALFORMED_PACKAGE, "Selected VPK has invalid Vita metadata")
                    }
                    InstallerPackageKind.PKG -> if (!VitaEmbeddedPackageSupport.hasPkgSignature(file)) {
                        throw InstallerExpansionException(InstallerExpansionFailure.MALFORMED_PACKAGE, "Selected PKG signature changed during staging")
                    }
                }
                PreparedEmbeddedPackage(candidate.copy(path = safePath), file, session)
            }
            InstallerPackageKind.CONTENT_DIRECTORY -> {
                val safePath = InstallerArchiveSafety.normalizeEntryPath(candidate.path)
                    ?: throw InstallerExpansionException(InstallerExpansionFailure.UNSUPPORTED_PATH, "Unsafe staged content path")
                val root = session.root.canonicalFile
                val directory = File(root, safePath).canonicalFile
                if (!directory.toPath().startsWith(root.toPath()) || !directory.isDirectory || Files.isSymbolicLink(directory.toPath())) {
                    throw InstallerExpansionException(InstallerExpansionFailure.UNSUPPORTED_PATH, "Selected content escaped private staging")
                }
                val output = VitaArchiveRepacker.repackDirectoryToInstallZip(
                    contentRoot = directory,
                    outputRoot = session.root,
                    maxBytes = maxAdditionalBytes,
                    freeSpaceReserveBytes = limits.freeSpaceReserveBytes,
                    isCancelled = isCancelled,
                    onProgress = onProgress
                ) ?: throw InstallerExpansionException(InstallerExpansionFailure.STAGING_IO, "Could not prepare extracted game content")
                PreparedEmbeddedPackage(candidate.copy(path = safePath), output, session)
            }
        }
    }

    override fun close() = session.close()

    private fun checkCancelled() {
        if (Thread.currentThread().isInterrupted || isCancelled()) {
            throw InstallerExpansionException(InstallerExpansionFailure.CANCELLED, "Archive preparation was cancelled")
        }
    }
}

/** Expands signature-identified containers into private staging, then recursively locates Vita packages. */
internal object VitaArchiveContainerSupport {
    fun scan(
        archivePath: String,
        stagingRoot: File,
        limits: InstallerExpansionLimits = InstallerExpansionLimits(),
        isCancelled: () -> Boolean = { false },
        onProgress: (EmbeddedPackageProgress) -> Unit = {}
    ): VitaArchivePackagePlan? {
        val source = File(archivePath)
        if (InstallerArchiveSafety.detectContainerFormat(source) == null) return null
        val session = InstallerExpansionSession.create(stagingRoot)
        try {
            val budget = ExpansionWalkBudget(limits)
            var layerIndex = 0
            val candidates = mutableListOf<InstallerPackageCandidate>()

            fun expandLayer(container: File, displayPath: String, depth: Int) {
                ensureNotCancelled(isCancelled)
                if (depth > limits.maxNestedDepth) {
                    throw InstallerExpansionException(InstallerExpansionFailure.DEPTH_LIMIT, "Nested archive depth limit exceeded")
                }
                if (++budget.archiveCount > limits.maxArchives) {
                    throw InstallerExpansionException(InstallerExpansionFailure.ARCHIVE_COUNT_LIMIT, "Nested archive count limit exceeded")
                }
                val output = File(session.root, "layer-${layerIndex++.toString().padStart(4, '0')}")
                val remainingBytes = (limits.maxExpandedBytes - budget.expandedBytes).coerceAtLeast(0L)
                val remainingEntries = (limits.maxEntries - budget.entryCount).coerceAtLeast(1)
                NativeArchiveExpander.expand(
                    input = container,
                    output = output,
                    limits = limits.copy(
                        maxEntries = remainingEntries,
                        maxExpandedBytes = remainingBytes.coerceAtLeast(1L)
                    ),
                    isCancelled = isCancelled
                ) { progress ->
                    onProgress(progress.copy(archiveEntryPath = "$displayPath/${progress.archiveEntryPath}"))
                }

                val nodes = output.walkTopDown().toList()
                budget.entryCount += nodes.size
                budget.expandedBytes += nodes.asSequence().filter(File::isFile).sumOf(File::length)
                if (budget.entryCount > limits.maxEntries || budget.expandedBytes > limits.maxExpandedBytes) {
                    throw InstallerExpansionException(InstallerExpansionFailure.EXPANDED_BYTES_LIMIT, "Nested archive expansion exceeds shared limits")
                }

                val files = nodes.asSequence().filter(File::isFile).sortedBy { it.relativeTo(output).invariantSeparatorsPath }.toList()
                files.forEach { file ->
                    ensureNotCancelled(isCancelled)
                    val relative = file.relativeTo(session.root).invariantSeparatorsPath
                    val safeRelative = InstallerArchiveSafety.normalizeEntryPath(relative)
                        ?: throw InstallerExpansionException(InstallerExpansionFailure.UNSUPPORTED_PATH, "Expanded member path is unsafe")
                    if (VitaEmbeddedPackageSupport.hasPkgSignature(file)) {
                        candidates += InstallerPackageCandidate(
                            safeRelative,
                            InstallerPackageKind.PKG,
                            file.length(),
                            "$displayPath/${file.relativeTo(output).invariantSeparatorsPath} · PKG"
                        )
                        return@forEach
                    }
                    val format = InstallerArchiveSafety.detectContainerFormat(file) ?: return@forEach
                    if (format == InstallerArchiveFormat.ZIP && VitaEmbeddedPackageSupport.isSafeInstallableVpk(
                            file,
                            session.root,
                            limits,
                            isCancelled
                        )) {
                        candidates += InstallerPackageCandidate(
                            safeRelative,
                            InstallerPackageKind.VPK,
                            file.length(),
                            "$displayPath/${file.relativeTo(output).invariantSeparatorsPath} · VPK"
                        )
                        return@forEach
                    }
                    if (format == InstallerArchiveFormat.ZIP && file.extension.equals("vpk", ignoreCase = true)) {
                        throw InstallerExpansionException(InstallerExpansionFailure.MALFORMED_PACKAGE, "Nested VPK is missing valid Vita install metadata")
                    }
                    val childDisplay = "$displayPath/${file.relativeTo(output).invariantSeparatorsPath}"
                    expandLayer(file, childDisplay, depth + 1)
                }

                VitaArchiveInspector.findInstallableContentRoots(output).forEach { contentRoot ->
                    val safePath = InstallerArchiveSafety.normalizeEntryPath(contentRoot.relativeTo(session.root).invariantSeparatorsPath)
                        ?: throw InstallerExpansionException(InstallerExpansionFailure.UNSUPPORTED_PATH, "Extracted game path is unsafe")
                    val size = contentRoot.walkTopDown().filter(File::isFile).sumOf(File::length)
                    candidates += InstallerPackageCandidate(
                        safePath,
                        InstallerPackageKind.CONTENT_DIRECTORY,
                        size,
                        "$displayPath/${contentRoot.relativeTo(output).invariantSeparatorsPath} · Vita content"
                    )
                }
            }

            expandLayer(source, source.name, 0)
            val ordered = candidates.distinctBy { it.path.lowercase(Locale.ROOT) }
                .sortedWith(compareBy<InstallerPackageCandidate> { it.displayName.lowercase(Locale.ROOT) }.thenBy { it.displayName })
            if (ordered.isEmpty()) {
                session.close()
                return null
            }
            return VitaArchivePackagePlan(
                ordered,
                session,
                limits,
                (limits.maxExpandedBytes - budget.expandedBytes).coerceAtLeast(0L),
                isCancelled,
                onProgress
            )
        } catch (error: Throwable) {
            session.close()
            throw error
        }
    }

    private fun ensureNotCancelled(isCancelled: () -> Boolean) {
        if (Thread.currentThread().isInterrupted || isCancelled()) {
            throw InstallerExpansionException(InstallerExpansionFailure.CANCELLED, "Archive preparation was cancelled")
        }
    }

    private data class ExpansionWalkBudget(
        val limits: InstallerExpansionLimits,
        var archiveCount: Int = 0,
        var entryCount: Int = 0,
        var expandedBytes: Long = 0
    )
}
