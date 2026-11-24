package com.sbro.emucorev.core

import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

/** Why a firmware download was rejected. */
class FirmwareDownloadException(
    val reason: Reason,
    message: String,
    cause: Throwable? = null,
    /** The failure came from the connection (retry on the next URL is sensible), not from the content. */
    val transport: Boolean = false
) : IOException(message, cause) {
    enum class Reason {
        /** Could not connect / the server answered with an error / the connection broke. */
        NETWORK,

        /** The stream ended before the announced (or pinned) length. */
        TRUNCATED,

        /** The data does not start with the SCEUF PUP magic. */
        NOT_PUP,

        /** Complete and a PUP, but not the pinned file. */
        HASH_MISMATCH,

        /** The finished file could not be moved into place. */
        FINALIZE,

        /** The caller cancelled. */
        CANCELLED
    }

    /** The file arrived but is not what was asked for (as opposed to a connectivity problem). */
    val isCorruptDownload: Boolean
        get() = reason == Reason.TRUNCATED || reason == Reason.NOT_PUP || reason == Reason.HASH_MISMATCH
}

/**
 * Downloads a [FirmwareSource] and refuses to hand out anything that is not exactly the pinned file.
 *
 * Streams to `<name>.part` while hashing, then checks: bytes received == Content-Length (when announced) == pinned size,
 * SCEUF magic, SHA-256. Only a file that passes is renamed to its final name; `.part` is deleted on every other path.
 * https is tried first; plain http is used only when the https connection itself fails (connect, TLS, HTTP error,
 * broken stream), never after a validation failure.
 */
class FirmwareDownloader(
    private val openConnection: (String) -> HttpURLConnection = { URL(it).openConnection() as HttpURLConnection },
    private val connectTimeoutMs: Int = 20_000,
    private val readTimeoutMs: Int = 30_000
) {
    fun download(
        source: FirmwareSource,
        directory: File,
        urls: List<String> = source.candidateUrls,
        isCancelled: () -> Boolean = { false },
        onProgress: (downloaded: Long, total: Long) -> Unit = { _, _ -> }
    ): File {
        directory.mkdirs()
        val target = File(directory, source.fileName)
        val part = File(directory, source.fileName + ".part")
        target.delete()
        part.delete()
        var lastNetworkError: FirmwareDownloadException? = null
        try {
            for (url in urls) {
                try {
                    fetch(url, source, part, isCancelled, onProgress)
                    if (!part.renameTo(target)) {
                        throw FirmwareDownloadException(FirmwareDownloadException.Reason.FINALIZE, "Could not finalize firmware download")
                    }
                    return target
                } catch (error: FirmwareDownloadException) {
                    part.delete()
                    if (error.reason != FirmwareDownloadException.Reason.NETWORK && !error.transport) throw error
                    lastNetworkError = error
                }
            }
            throw lastNetworkError ?: FirmwareDownloadException(FirmwareDownloadException.Reason.NETWORK, "No download URL")
        } finally {
            part.delete()
        }
    }

    private fun fetch(
        url: String,
        source: FirmwareSource,
        part: File,
        isCancelled: () -> Boolean,
        onProgress: (Long, Long) -> Unit
    ) {
        val connection = try {
            openConnection(url)
        } catch (error: IOException) {
            throw FirmwareDownloadException(FirmwareDownloadException.Reason.NETWORK, "Cannot open $url", error)
        }
        try {
            connection.connectTimeout = connectTimeoutMs
            connection.readTimeout = readTimeoutMs
            connection.requestMethod = "GET"
            connection.instanceFollowRedirects = true
            connection.setRequestProperty("User-Agent", "EmuCoreV Android")
            // A transparently compressed body has no usable Content-Length and cannot be pinned to a size.
            connection.setRequestProperty("Accept-Encoding", "identity")

            var announced = -1L
            val digest = MessageDigest.getInstance("SHA-256")
            val magic = ByteArray(MAGIC.size)
            var magicRead = 0
            var total = 0L
            try {
                connection.connect()
                val code = connection.responseCode
                if (code !in 200..299) {
                    throw FirmwareDownloadException(FirmwareDownloadException.Reason.NETWORK, "HTTP $code")
                }
                announced = connection.contentLengthLong
                onProgress(0L, if (announced > 0) announced else source.sizeBytes)
                connection.inputStream.use { input ->
                    FileOutputStream(part).use { output ->
                        val buffer = ByteArray(64 * 1024)
                        var lastEmitted = 0L
                        while (true) {
                            if (isCancelled()) {
                                throw FirmwareDownloadException(FirmwareDownloadException.Reason.CANCELLED, "Cancelled")
                            }
                            val read = input.read(buffer)
                            if (read < 0) break
                            if (read == 0) continue
                            output.write(buffer, 0, read)
                            digest.update(buffer, 0, read)
                            if (magicRead < magic.size) {
                                val take = minOf(read, magic.size - magicRead)
                                System.arraycopy(buffer, 0, magic, magicRead, take)
                                magicRead += take
                            }
                            total += read
                            if (total - lastEmitted > PROGRESS_STEP) {
                                lastEmitted = total
                                onProgress(total, if (announced > 0) announced else source.sizeBytes)
                            }
                        }
                        output.flush()
                        output.fd.sync()
                    }
                }
            } catch (error: FirmwareDownloadException) {
                throw error
            } catch (error: IOException) {
                throw FirmwareDownloadException(FirmwareDownloadException.Reason.NETWORK, "Download interrupted: ${error.message}", error)
            }

            validate(source, announced, total, magic, magicRead, digest.digest())
            onProgress(total, total)
        } finally {
            connection.disconnect()
        }
    }

    private fun validate(source: FirmwareSource, announced: Long, total: Long, magic: ByteArray, magicRead: Int, hash: ByteArray) {
        if (announced > 0 && total != announced) {
            throw FirmwareDownloadException(
                FirmwareDownloadException.Reason.TRUNCATED,
                "Received $total of $announced bytes",
                transport = true // the server announced more than it delivered: the connection ended early
            )
        }
        if (total != source.sizeBytes) {
            throw FirmwareDownloadException(
                FirmwareDownloadException.Reason.TRUNCATED,
                "Received $total bytes, expected ${source.sizeBytes}"
            )
        }
        if (magicRead < MAGIC.size || !magic.contentEquals(MAGIC)) {
            throw FirmwareDownloadException(FirmwareDownloadException.Reason.NOT_PUP, "Not a PUP (missing SCEUF magic)")
        }
        val hex = hash.joinToString("") { "%02x".format(it) }
        if (!hex.equals(source.sha256, ignoreCase = true)) {
            throw FirmwareDownloadException(FirmwareDownloadException.Reason.HASH_MISMATCH, "SHA-256 mismatch: $hex")
        }
    }

    private companion object {
        val MAGIC = "SCEUF".toByteArray(Charsets.US_ASCII)
        const val PROGRESS_STEP = 512L * 1024
    }
}
