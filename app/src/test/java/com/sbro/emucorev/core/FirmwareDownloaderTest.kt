package com.sbro.emucorev.core

import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import java.io.File
import java.io.OutputStream
import java.net.ServerSocket
import java.net.Socket
import java.nio.file.Files
import java.security.MessageDigest
import java.util.concurrent.atomic.AtomicInteger
import kotlin.concurrent.thread

class FirmwareDownloaderTest {
    private lateinit var dir: File
    private val servers = mutableListOf<MockServer>()

    /** Tiny HTTP/1.0-style server: reads the request head, lets [respond] write the raw response, closes. */
    private class MockServer(private val respond: (OutputStream) -> Unit) {
        private val socket = ServerSocket(0)
        val requests = AtomicInteger()
        val url: String get() = "http://127.0.0.1:${socket.localPort}/firmware.pup"
        private val worker = thread(isDaemon = true) {
            while (!socket.isClosed) {
                val client: Socket = try {
                    socket.accept()
                } catch (_: Exception) {
                    break
                }
                requests.incrementAndGet()
                runCatching {
                    client.getInputStream().bufferedReader().let { reader ->
                        while (true) {
                            val line = reader.readLine() ?: break
                            if (line.isEmpty()) break
                        }
                    }
                    respond(client.getOutputStream())
                    client.getOutputStream().flush()
                }
                runCatching { client.close() }
            }
        }

        fun close() {
            runCatching { socket.close() }
            worker.join(2000)
        }
    }

    private fun serve(respond: (OutputStream) -> Unit) = MockServer(respond).also { servers += it }

    private fun fakePup(size: Int, fill: Int = 7): ByteArray =
        ByteArray(size) { fill.toByte() }.also { "SCEUF".toByteArray().copyInto(it) }

    private fun sha256(data: ByteArray) = MessageDigest.getInstance("SHA-256").digest(data).joinToString("") { "%02x".format(it) }

    private fun sourceFor(data: ByteArray) = FirmwareSource(
        kind = FirmwareKind.Base,
        version = "test",
        url = "http://unused.invalid/x.pup",
        sizeBytes = data.size.toLong(),
        sha256 = sha256(data),
        fileName = "TEST.PUP"
    )

    private fun ok(body: ByteArray, announce: Long = body.size.toLong()): (OutputStream) -> Unit = { out ->
        out.write("HTTP/1.1 200 OK\r\nContent-Length: $announce\r\nConnection: close\r\n\r\n".toByteArray())
        out.write(body)
    }

    private fun assertNoLeftovers() {
        assertFalse("final file must not exist", File(dir, "TEST.PUP").exists())
        assertFalse(".part must be deleted", File(dir, "TEST.PUP.part").exists())
    }

    private fun expectFailure(reason: FirmwareDownloadException.Reason, block: () -> Unit) {
        try {
            block()
            fail("expected $reason")
        } catch (error: FirmwareDownloadException) {
            assertEquals(error.message, reason, error.reason)
        }
    }

    @Before
    fun setUp() {
        dir = Files.createTempDirectory("fwdl").toFile()
    }

    @After
    fun tearDown() {
        servers.forEach { it.close() }
        dir.deleteRecursively()
    }

    @Test
    fun goodDownloadIsKeptAndReportsProgress() {
        val data = fakePup(1_300_000)
        val server = serve(ok(data))
        val progress = mutableListOf<Pair<Long, Long>>()
        val file = FirmwareDownloader().download(sourceFor(data), dir, listOf(server.url)) { done, total -> progress += done to total }
        assertTrue(file.isFile)
        assertEquals("TEST.PUP", file.name)
        assertTrue(data.contentEquals(file.readBytes()))
        assertFalse(File(dir, "TEST.PUP.part").exists())
        assertEquals(data.size.toLong() to data.size.toLong(), progress.last())
        assertTrue(progress.size >= 2)
    }

    @Test
    fun serverClosingEarlyFailsAndRemovesPart() {
        val data = fakePup(900_000)
        // Announces the full length but the connection ends after 40%.
        val server = serve(ok(data.copyOf(360_000), announce = data.size.toLong()))
        // HttpURLConnection reports a premature EOF on a fixed-length body as an IOException (NETWORK); an implementation
        // that returns -1 instead is caught by the length check (TRUNCATED). Either way the download must fail.
        try {
            FirmwareDownloader().download(sourceFor(data), dir, listOf(server.url))
            fail("a truncated stream must not be accepted")
        } catch (error: FirmwareDownloadException) {
            assertTrue(error.reason.toString(), error.reason == FirmwareDownloadException.Reason.NETWORK || error.reason == FirmwareDownloadException.Reason.TRUNCATED)
        }
        assertNoLeftovers()
    }

    @Test
    fun shortBodyWithoutContentLengthStillFailsOnPinnedSize() {
        val data = fakePup(500_000)
        val server = serve { out ->
            out.write("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n".toByteArray())
            out.write(data, 0, 123_456)
        }
        expectFailure(FirmwareDownloadException.Reason.TRUNCATED) {
            FirmwareDownloader().download(sourceFor(data), dir, listOf(server.url))
        }
        assertNoLeftovers()
    }

    @Test
    fun wrongMagicIsRejected() {
        val good = fakePup(200_000)
        val wrong = good.copyOf().also { "<html".toByteArray().copyInto(it) }
        val server = serve(ok(wrong))
        expectFailure(FirmwareDownloadException.Reason.NOT_PUP) {
            FirmwareDownloader().download(sourceFor(good), dir, listOf(server.url))
        }
        assertNoLeftovers()
    }

    @Test
    fun hashMismatchIsRejected() {
        val good = fakePup(200_000)
        val tampered = good.copyOf().also { it[150_000] = 99 }
        val server = serve(ok(tampered))
        expectFailure(FirmwareDownloadException.Reason.HASH_MISMATCH) {
            FirmwareDownloader().download(sourceFor(good), dir, listOf(server.url))
        }
        assertNoLeftovers()
    }

    @Test
    fun httpErrorIsANetworkFailure() {
        val data = fakePup(1000)
        val server = serve { out -> out.write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n".toByteArray()) }
        expectFailure(FirmwareDownloadException.Reason.NETWORK) {
            FirmwareDownloader().download(sourceFor(data), dir, listOf(server.url))
        }
        assertNoLeftovers()
    }

    @Test
    fun fallsBackToTheSecondUrlOnlyWhenTheFirstCannotConnect() {
        val data = fakePup(300_000)
        val good = serve(ok(data))
        val deadPort = ServerSocket(0).use { it.localPort } // closed again: connection refused
        val file = FirmwareDownloader().download(
            sourceFor(data), dir,
            listOf("http://127.0.0.1:$deadPort/x", good.url)
        )
        assertTrue(data.contentEquals(file.readBytes()))
        assertEquals(1, good.requests.get())
    }

    @Test
    fun aCorruptFirstUrlDoesNotFallBackToTheSecond() {
        val good = fakePup(300_000)
        val tampered = good.copyOf().also { it[200_000] = 1 }
        val first = serve(ok(tampered))
        val second = serve(ok(good))
        expectFailure(FirmwareDownloadException.Reason.HASH_MISMATCH) {
            FirmwareDownloader().download(sourceFor(good), dir, listOf(first.url, second.url))
        }
        assertEquals(1, first.requests.get())
        assertEquals(0, second.requests.get())
        assertNoLeftovers()
    }

    @Test
    fun brokenStreamOnTheFirstUrlFallsBackAndSucceeds() {
        val good = fakePup(300_000)
        // Resets in the middle of the announced body: an I/O error, not a validation result.
        val first = serve { out ->
            out.write("HTTP/1.1 200 OK\r\nContent-Length: ${good.size}\r\nConnection: close\r\n\r\n".toByteArray())
            out.write(good, 0, 1000)
            throw java.io.IOException("simulated reset")
        }
        val second = serve(ok(good))
        val file = FirmwareDownloader().download(sourceFor(good), dir, listOf(first.url, second.url))
        assertTrue(good.contentEquals(file.readBytes()))
        assertEquals(1, second.requests.get())
    }

    @Test
    fun cancelStopsTheTransferAndDeletesPart() {
        val data = fakePup(3_000_000)
        val server = serve(ok(data))
        var calls = 0
        expectFailure(FirmwareDownloadException.Reason.CANCELLED) {
            FirmwareDownloader().download(sourceFor(data), dir, listOf(server.url), isCancelled = { ++calls > 3 })
        }
        assertNoLeftovers()
    }

    @Test
    fun staleFilesFromEarlierAttemptsAreReplaced() {
        File(dir, "TEST.PUP").writeText("old")
        File(dir, "TEST.PUP.part").writeText("old part")
        val data = fakePup(50_000)
        val server = serve(ok(data))
        val file = FirmwareDownloader().download(sourceFor(data), dir, listOf(server.url))
        assertTrue(data.contentEquals(file.readBytes()))
    }
}
