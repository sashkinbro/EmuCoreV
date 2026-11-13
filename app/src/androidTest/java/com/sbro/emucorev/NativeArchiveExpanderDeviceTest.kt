package com.sbro.emucorev

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorev.core.InstallerArchiveNativeFailure
import com.sbro.emucorev.core.InstallerExpansionLimits
import com.sbro.emucorev.core.NativeArchiveExpander
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.libsdl.app.SDL
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.UUID
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/** Calls the production JNI adapter and real libarchive decoders on Android. */
@RunWith(AndroidJUnit4::class)
class NativeArchiveExpanderDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext

    @Before
    fun loadNativeLibrary() {
        SDL.setContext(context)
        SDL.loadLibrary("ECV", context)
        SDL.setupJNI()
    }

    @Test
    fun genuineSevenZipRar4AndRar5FixturesDecodeThroughJni() {
        withScratch { scratch ->
            assertFixtureDecodes(scratch, "stored.7z", "payload/hello.txt", "Hello archive backend".toByteArray())
            assertFixtureDecodes(scratch, "lzma2.7z", "payload/hello.txt", "Hello archive backend".toByteArray())
            assertFixtureDecodes(scratch, "rar4-singlefile.rar", "test.txt", "test text document\r\n".toByteArray())

            val rar5Expected = ByteArrayOutputStream().also { output ->
                for (value in 1..300) {
                    val decoded = maxOf(0, value * value - 3 * value + 1)
                    output.write(ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(decoded).array())
                }
            }.toByteArray()
            assertFixtureDecodes(scratch, "rar5-compressed.rar", "test.bin", rar5Expected)
        }
    }

    @Test
    fun unsafeMemberAndCancellationRollBackStaging() {
        withScratch { scratch ->
            val archive = File(scratch, "traversal.zip")
            ZipOutputStream(archive.outputStream()).use { zip ->
                zip.putNextEntry(ZipEntry("../escaped.txt"))
                zip.write("must not escape".toByteArray())
                zip.closeEntry()
            }
            val unsafeOutput = File(scratch, "unsafe-output")
            assertEquals(
                NativeArchiveExpander.UNSAFE_PATH,
                runCatching { expand(archive, unsafeOutput) }.exceptionOrNull().status()
            )
            assertFalse(unsafeOutput.exists())
            assertFalse(File(scratch, "escaped.txt").exists())

            val validArchive = copyFixture(scratch, "stored.7z")
            val cancelledOutput = File(scratch, "cancelled-output")
            assertEquals(
                NativeArchiveExpander.CANCELLED,
                runCatching { expand(validArchive, cancelledOutput, isCancelled = { true }) }
                    .exceptionOrNull().status()
            )
            assertFalse(cancelledOutput.exists())
        }
    }

    @Test
    fun preexistingStagingContentsArePreserved() {
        withScratch { scratch ->
            val archive = copyFixture(scratch, "stored.7z")
            val output = File(scratch, "occupied")
            assertTrue(output.mkdir())
            val marker = File(output, "keep.txt").apply { writeText("keep") }

            assertEquals(
                NativeArchiveExpander.IO_ERROR,
                runCatching { expand(archive, output) }.exceptionOrNull().status()
            )
            assertEquals("keep", marker.readText())
        }
    }

    private fun assertFixtureDecodes(scratch: File, fixture: String, expectedPath: String, expected: ByteArray) {
        val archive = copyFixture(scratch, fixture)
        val output = File(scratch, "out-${UUID.randomUUID()}")
        expand(archive, output)
        assertArrayEquals(expected, File(output, expectedPath).readBytes())
    }

    private fun copyFixture(destination: File, name: String): File {
        val output = File(destination, name)
        instrumentation.context.assets.open("archive-expander/$name").use { input ->
            output.outputStream().use { destination -> input.copyTo(destination) }
        }
        return output
    }

    private fun expand(input: File, output: File, isCancelled: () -> Boolean = { false }) {
        NativeArchiveExpander.expand(
            input = input,
            output = output,
            limits = InstallerExpansionLimits(
                maxEntries = 100,
                maxExpandedBytes = 2L * 1024 * 1024,
                maxDecoderMemoryBytes = 256L * 1024 * 1024,
                freeSpaceReserveBytes = 0
            ),
            isCancelled = isCancelled
        )
    }

    private fun withScratch(block: (File) -> Unit) {
        val root = File(context.cacheDir, "archive-expander-test-${UUID.randomUUID()}")
        assertTrue(root.mkdirs())
        try {
            block(root)
        } finally {
            root.deleteRecursively()
        }
    }

    private fun Throwable?.status(): Int = (this as? InstallerArchiveNativeFailure)?.status ?: -1
}
