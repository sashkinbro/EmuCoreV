package com.sbro.emucorev.core

import java.io.File

internal fun interface NativeArchiveCancellation {
    fun isCancelled(): Boolean
}

internal fun interface NativeArchiveProgressListener {
    fun onProgress(path: String, copiedBytes: Long, totalBytes: Long)
}

/** JNI entry point implemented beside the archive adapter; the loaded ECV library exports it. */
internal object NativeArchiveExpander {
    const val OK = 0
    const val UNSUPPORTED_FORMAT = 1
    const val MALFORMED_ARCHIVE = 2
    const val UNSAFE_PATH = 3
    const val DUPLICATE_ENTRY = 4
    const val LINK_OR_SPECIAL_ENTRY = 5
    const val ENTRY_LIMIT = 6
    const val EXPANDED_BYTES_LIMIT = 7
    const val DECODER_MEMORY_LIMIT = 8
    const val INSUFFICIENT_SPACE = 9
    const val CANCELLED = 10
    const val IO_ERROR = 11
    const val ENCRYPTED_ARCHIVE = 12

    @JvmStatic
    external fun nativeExtract(
        inputPath: String,
        stagingRoot: String,
        maxEntries: Int,
        maxExpandedBytes: Long,
        freeSpaceReserveBytes: Long,
        maxDecoderMemoryBytes: Long,
        cancellation: NativeArchiveCancellation,
        progress: NativeArchiveProgressListener
    ): Int

    fun expand(
        input: File,
        output: File,
        limits: InstallerExpansionLimits = InstallerExpansionLimits(),
        isCancelled: () -> Boolean = { false },
        onProgress: (EmbeddedPackageProgress) -> Unit = {}
    ) {
        val status = nativeExtract(
            input.absolutePath,
            output.absolutePath,
            limits.maxEntries,
            limits.maxExpandedBytes,
            limits.freeSpaceReserveBytes,
            limits.maxDecoderMemoryBytes,
            NativeArchiveCancellation(isCancelled),
            NativeArchiveProgressListener { path, copied, total ->
                onProgress(EmbeddedPackageProgress(copied, total, path))
            }
        )
        if (status != OK) throw InstallerArchiveNativeFailure(status)
    }
}

internal class InstallerArchiveNativeFailure(val status: Int) : java.io.IOException("Archive expansion failed ($status)")
