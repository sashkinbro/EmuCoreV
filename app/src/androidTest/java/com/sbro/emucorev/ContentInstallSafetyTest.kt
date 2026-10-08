package com.sbro.emucorev

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorev.core.VitaInstallBridge
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.libsdl.app.SDL
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.UUID
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/** Exercises the real native installer in a disposable filesystem. */
@RunWith(AndroidJUnit4::class)
class ContentInstallSafetyTest {
    private val context = InstrumentationRegistry.getInstrumentation().targetContext

    @Before
    fun loadNativeLibrary() {
        SDL.setContext(context)
        SDL.loadLibrary("Vita3K", context)
        SDL.setupJNI()
    }

    @Test
    fun emptyTitleIdCannotReplaceInstalledApps() {
        withInstallRoot { root ->
            val sentinel = File(root, "vita/ux0/app/PCSE12345/keep.txt")
            sentinel.parentFile!!.mkdirs()
            sentinel.writeText("installed game")
            val content = createContent(root, "")

            val count = install(root, content)

            assertTrue("An invalid package removed an existing game", sentinel.isFile)
            assertEquals("installed game", sentinel.readText())
            assertEquals(0, count)
        }
    }

    @Test
    fun validTitleInstallsWithoutReplacingOtherApps() {
        withInstallRoot { root ->
            val sentinel = File(root, "vita/ux0/app/PCSE12345/keep.txt")
            sentinel.parentFile!!.mkdirs()
            sentinel.writeText("installed game")
            val content = createContent(root, "PCSE54321")

            assertEquals(1, install(root, content))
            assertEquals("installed game", sentinel.readText())
            assertEquals("payload", File(root, "vita/ux0/app/PCSE54321/data.txt").readText())
        }
    }

    @Test
    fun missingTitleIdCannotInheritPreviousArchiveTitle() {
        withInstallRoot { root ->
            val valid = createContent(File(root, "valid"), "PCSE54321")
            val invalid = createContent(File(root, "invalid"), null)
            File(invalid, "data.txt").writeText("invalid payload")
            val archive = File(root, "bundle.zip")
            ZipOutputStream(archive.outputStream()).use { zip ->
                listOf("first" to valid, "second" to invalid).forEach { (prefix, content) ->
                    content.walkTopDown().filter { it.isFile }.forEach { file ->
                        zip.putNextEntry(ZipEntry("$prefix/${file.relativeTo(content).invariantSeparatorsPath}"))
                        file.inputStream().use { it.copyTo(zip) }
                        zip.closeEntry()
                    }
                }
            }

            assertEquals("Only the valid content should be installed", 1, install(root, archive))
            assertEquals("payload", File(root, "vita/ux0/app/PCSE54321/data.txt").readText())
        }
    }

    private fun withInstallRoot(test: (File) -> Unit) {
        val root = File(context.cacheDir, "install-regression-${UUID.randomUUID()}")
        check(root.mkdirs())
        try {
            test(root)
        } finally {
            root.deleteRecursively()
        }
    }

    private fun install(root: File, content: File): Int {
        val method = VitaInstallBridge::class.java.getDeclaredMethod(
            "nativeInstallContent", String::class.java, String::class.java,
            String::class.java, Int::class.javaPrimitiveType
        ).apply { isAccessible = true }
        return method.invoke(
            VitaInstallBridge, File(root, "vita").absolutePath,
            File(root, "cache").absolutePath, content.absolutePath, 1
        ) as Int
    }

    private fun createContent(root: File, titleId: String?): File {
        val content = File(root, "source")
        val metadata = File(content, "sce_sys/param.sfo")
        metadata.parentFile!!.mkdirs()
        val entries = buildList {
            add("CATEGORY" to "gd")
            add("TITLE" to "Test content")
            if (titleId != null) add("TITLE_ID" to titleId)
        }
        val keys = entries.map { (key, _) -> (key + '\u0000').toByteArray() }
        val values = entries.map { (_, value) -> (value + '\u0000').toByteArray() }
        val keyStart = 20 + entries.size * 16
        val dataStart = (keyStart + keys.sumOf { it.size } + 3) and -4
        val sfo = ByteBuffer.allocate(dataStart + values.sumOf { it.size }).order(ByteOrder.LITTLE_ENDIAN)
        sfo.putInt(0x46535000).putInt(0x101).putInt(keyStart).putInt(dataStart).putInt(entries.size)
        var keyOffset = 0
        var dataOffset = 0
        for (index in entries.indices) {
            sfo.putShort(keyOffset.toShort()).putShort(0x0204)
                .putInt(values[index].size).putInt(values[index].size).putInt(dataOffset)
            keyOffset += keys[index].size
            dataOffset += values[index].size
        }
        sfo.position(keyStart)
        keys.forEach { sfo.put(it) }
        sfo.position(dataStart)
        values.forEach { sfo.put(it) }
        metadata.writeBytes(sfo.array())
        File(content, "data.txt").writeText("payload")
        return content
    }
}
