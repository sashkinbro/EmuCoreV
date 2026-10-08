package com.sbro.emucorev

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorev.core.VitaInstallBridge
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.libsdl.app.SDL
import java.io.File
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.UUID
import java.util.zip.CRC32
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

    @Test
    fun archiveTraversalEntryCannotWriteOutsideInstalledTitle() {
        withInstallRoot { root ->
            val outside = File(root, "escaped.txt").apply { writeText("keep") }
            val archive = createArchive(root, "PCSE54321", traversalEntry = true)

            val result = install(root, archive)
            assertTrue("Archive wrote outside the title directory", outside.isFile)
            assertEquals("keep", outside.readText())
            assertEquals(0, result)
        }
    }

    @Test
    fun corruptArchiveEntryPreservesPreviouslyInstalledTitle() {
        withInstallRoot { root ->
            val sentinel = File(root, "vita/ux0/app/PCSE54321/keep.txt")
            sentinel.parentFile!!.mkdirs()
            sentinel.writeText("installed game")
            val archive = createArchive(root, "PCSE54321", corruptExecutable = true)

            val result = install(root, archive)
            assertTrue(sentinel.isFile)
            assertEquals("installed game", sentinel.readText())
            assertEquals(0, result)
        }
    }

    @Test
    fun validArchiveAtomicallyReplacesInstalledTitle() {
        withInstallRoot { root ->
            val oldFile = File(root, "vita/ux0/app/PCSE54321/old.txt")
            oldFile.parentFile!!.mkdirs()
            oldFile.writeText("old install")
            val archive = createArchive(root, "PCSE54321")

            assertEquals(1, install(root, archive))
            assertFalse(oldFile.exists())
            assertEquals("new executable", File(oldFile.parentFile, "eboot.bin").readText())
        }
    }

    @Test
    fun validPatchOverlaysExistingTitleWithoutDroppingUnpatchedFiles() {
        withInstallRoot { root ->
            val installed = File(root, "vita/ux0/app/PCSE54321")
            installed.mkdirs()
            File(installed, "keep.txt").writeText("base game")
            File(installed, "eboot.bin").writeText("old executable")
            val archive = createArchive(root, "PCSE54321", category = "gp")

            assertEquals(1, install(root, archive))
            assertEquals("base game", File(installed, "keep.txt").readText())
            assertEquals("new executable", File(installed, "eboot.bin").readText())
        }
    }

    @Test
    fun failedPatchOverlayLeavesInstalledTitleUntouched() {
        withInstallRoot { root ->
            val installed = File(root, "vita/ux0/app/PCSE54321")
            installed.mkdirs()
            File(installed, "keep.txt").writeText("base game")
            File(installed, "eboot.bin").writeText("old executable")
            File(installed, "z_conflict").writeText("existing file")
            val archive = createArchive(
                root,
                "PCSE54321",
                category = "gp",
                additionalEntries = listOf("z_conflict/child.txt" to "cannot overlay".toByteArray())
            )

            val result = install(root, archive)
            assertEquals("base game", File(installed, "keep.txt").readText())
            assertEquals("old executable", File(installed, "eboot.bin").readText())
            assertEquals("existing file", File(installed, "z_conflict").readText())
            assertEquals(0, result)
        }
    }

    @Test
    fun rootContentDoesNotAbsorbNestedContentFromMultiContentArchive() {
        withInstallRoot { root ->
            val rootContent = createContent(File(root, "root-content"), "PCSE54321")
            val nestedContent = createContent(File(root, "nested-content"), "PCSE67890")
            val archive = File(root, "${UUID.randomUUID()}-bundle.zip")
            ZipOutputStream(archive.outputStream()).use { zip ->
                addContentFiles(zip, rootContent, "")
                addContentFiles(zip, nestedContent, "addon/")
            }

            assertEquals(2, install(root, archive))
            assertEquals("payload", File(root, "vita/ux0/app/PCSE54321/data.txt").readText())
            assertFalse(File(root, "vita/ux0/app/PCSE54321/addon").exists())
            assertEquals("payload", File(root, "vita/ux0/app/PCSE67890/data.txt").readText())
        }
    }

    @Test
    fun markerLikeMetadataFilenameDoesNotHideRootAssets() {
        withInstallRoot { root ->
            val archive = createArchive(
                root,
                "PCSE54321",
                additionalEntries = listOf(
                    "assets/sce_sys/param.sfo.bak" to "not metadata".toByteArray(),
                    "assets/sce_module/steroid.suprx.bak" to "ordinary backup asset".toByteArray(),
                    "assets/data.bin" to "root asset".toByteArray()
                )
            )

            val result = install(root, archive)
            assertEquals("root asset", File(root, "vita/ux0/app/PCSE54321/assets/data.bin").readText())
            assertEquals("ordinary backup asset", File(root, "vita/ux0/app/PCSE54321/assets/sce_module/steroid.suprx.bak").readText())
            assertEquals(1, result)
        }
    }

    @Test
    fun uppercaseSfoMetadataIsInstalledAtCanonicalPath() {
        withInstallRoot { root ->
            val archive = createArchive(root, "PCSE54321", uppercaseSfo = true)

            assertEquals(1, install(root, archive))
            assertTrue(File(root, "vita/ux0/app/PCSE54321/sce_sys/param.sfo").isFile)
            assertFalse(File(root, "vita/ux0/app/PCSE54321/SCE_SYS/PARAM.SFO").exists())
        }
    }

    @Test
    fun uppercaseThemeMetadataIsInstalledAtCanonicalPath() {
        withInstallRoot { root ->
            val archive = File(root, "${UUID.randomUUID()}.vpk")
            ZipOutputStream(archive.outputStream()).use { zip ->
                zip.putNextEntry(ZipEntry("THEME.XML"))
                zip.write("""<theme><InfomationProperty><m_contentId>SAFE_THEME</m_contentId><m_title><m_default>Safe theme</m_default></m_title></InfomationProperty></theme>""".toByteArray())
                zip.closeEntry()
            }

            assertEquals(1, install(root, archive))
            assertTrue(File(root, "vita/ux0/theme/SAFE_THEME/theme.xml").isFile)
            assertFalse(File(root, "vita/ux0/theme/SAFE_THEME/THEME.XML").exists())
        }
    }

    @Test
    fun explicitRelativeRootDirectoryDoesNotRejectArchive() {
        withInstallRoot { root ->
            val archive = createArchive(root, "PCSE54321", relativeRootPrefix = true)

            assertEquals(1, install(root, archive))
            assertEquals("new executable", File(root, "vita/ux0/app/PCSE54321/eboot.bin").readText())
        }
    }

    @Test
    fun caseVariantDuplicateMetadataIsRejectedWithoutReplacingTitle() {
        withInstallRoot { root ->
            val sentinel = File(root, "vita/ux0/app/PCSE54321/keep.txt")
            sentinel.parentFile!!.mkdirs()
            sentinel.writeText("installed game")
            val archive = createArchive(
                root,
                "PCSE54321",
                additionalEntries = listOf("SCE_SYS/PARAM.SFO" to "conflicting metadata".toByteArray())
            )

            val result = install(root, archive)
            assertTrue(sentinel.isFile)
            assertEquals("installed game", sentinel.readText())
            assertEquals(0, result)
        }
    }

    @Test
    fun unsafeTitleIdCannotReplaceOtherVitaContent() {
        withInstallRoot { root ->
            val sentinel = File(root, "vita/ux0/escape/keep.txt")
            sentinel.parentFile!!.mkdirs()
            sentinel.writeText("existing content")
            val archive = createArchive(root, "../escape")

            val result = install(root, archive)
            assertTrue(sentinel.isFile)
            assertEquals("existing content", sentinel.readText())
            assertEquals(0, result)
        }
    }

    @Test
    fun unsafeDlcContentIdCannotReplaceOtherVitaContent() {
        withInstallRoot { root ->
            val sentinel = File(root, "vita/ux0/escape/keep.txt")
            sentinel.parentFile!!.mkdirs()
            sentinel.writeText("existing content")
            val archive = createArchive(
                root,
                "PCSE54321",
                category = "ac",
                contentId = "UP0000-PCSE54321_00-../../escape"
            )

            val result = install(root, archive)
            assertTrue(sentinel.isFile)
            assertEquals("existing content", sentinel.readText())
            assertEquals(0, result)
        }
    }

    @Test
    fun unsafeThemeIdCannotReplaceInstalledApps() {
        withInstallRoot { root ->
            val sentinel = File(root, "vita/ux0/app/PCSE54321/keep.txt")
            sentinel.parentFile!!.mkdirs()
            sentinel.writeText("installed game")
            val archive = File(root, "${UUID.randomUUID()}.vpk")
            ZipOutputStream(archive.outputStream()).use { zip ->
                zip.putNextEntry(ZipEntry("theme.xml"))
                zip.write("""<theme><InfomationProperty><m_contentId>../app</m_contentId><m_title><m_default>Unsafe theme</m_default></m_title></InfomationProperty></theme>""".toByteArray())
                zip.closeEntry()
            }

            val result = install(root, archive)
            assertTrue(sentinel.isFile)
            assertEquals("installed game", sentinel.readText())
            assertEquals(0, result)
        }
    }

    @Test
    fun shortDlcContentIdIsRejectedWithoutChangingExistingContent() {
        withInstallRoot { root ->
            val sentinel = File(root, "vita/ux0/addcont/PCSE54321/EXISTING/keep.txt")
            sentinel.parentFile!!.mkdirs()
            sentinel.writeText("installed DLC")
            val archive = createArchive(
                root,
                "PCSE54321",
                category = "ac",
                contentId = "short"
            )

            val result = install(root, archive)
            assertTrue(sentinel.isFile)
            assertEquals("installed DLC", sentinel.readText())
            assertEquals(0, result)
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

    private fun createArchive(
        root: File,
        titleId: String,
        traversalEntry: Boolean = false,
        corruptExecutable: Boolean = false,
        category: String = "gd",
        contentId: String? = null,
        uppercaseSfo: Boolean = false,
        relativeRootPrefix: Boolean = false,
        additionalEntries: List<Pair<String, ByteArray>> = emptyList()
    ): File {
        val content = createContent(File(root, "archive-source"), titleId, category, contentId)
        val output = File(root, "${UUID.randomUUID()}.vpk")
        val executable = "new executable".toByteArray()
        val archiveBytes = ByteArrayOutputStream().use { bytes ->
            ZipOutputStream(bytes).use { zip ->
                if (relativeRootPrefix) {
                    zip.putNextEntry(ZipEntry("./"))
                    zip.closeEntry()
                }
                content.walkTopDown().filter { it.isFile }.forEach { file ->
                    val sourceName = file.relativeTo(content).invariantSeparatorsPath
                    val metadataName = if (uppercaseSfo && sourceName == "sce_sys/param.sfo") "SCE_SYS/PARAM.SFO" else sourceName
                    val name = if (relativeRootPrefix) "./$metadataName" else metadataName
                    zip.putNextEntry(ZipEntry(name))
                    file.inputStream().use { it.copyTo(zip) }
                    zip.closeEntry()
                }

                val crc = CRC32().apply { update(executable) }
                zip.putNextEntry(ZipEntry(if (relativeRootPrefix) "./eboot.bin" else "eboot.bin").apply {
                    method = ZipEntry.STORED
                    size = executable.size.toLong()
                    compressedSize = executable.size.toLong()
                    this.crc = crc.value
                })
                zip.write(executable)
                zip.closeEntry()

                if (traversalEntry) {
                    zip.putNextEntry(ZipEntry("../../../../escaped.txt"))
                    zip.write("outside".toByteArray())
                    zip.closeEntry()
                }
                additionalEntries.forEach { (name, payload) ->
                    zip.putNextEntry(ZipEntry(name))
                    zip.write(payload)
                    zip.closeEntry()
                }
            }
            bytes.toByteArray()
        }

        if (corruptExecutable) {
            val payloadOffset = archiveBytes.indexOfSubsequence(executable)
            check(payloadOffset >= 0) { "Stored executable payload was not found in ZIP bytes" }
            archiveBytes[payloadOffset] = (archiveBytes[payloadOffset].toInt() xor 1).toByte()
        }
        output.writeBytes(archiveBytes)
        return output
    }

    private fun addContentFiles(zip: ZipOutputStream, content: File, prefix: String) {
        content.walkTopDown().filter { it.isFile }.forEach { file ->
            val name = prefix + file.relativeTo(content).invariantSeparatorsPath
            zip.putNextEntry(ZipEntry(name))
            file.inputStream().use { it.copyTo(zip) }
            zip.closeEntry()
        }
    }

    private fun ByteArray.indexOfSubsequence(needle: ByteArray): Int {
        if (needle.isEmpty() || needle.size > size) return -1
        for (start in 0..(size - needle.size)) {
            if (needle.indices.all { this[start + it] == needle[it] }) return start
        }
        return -1
    }

    private fun createContent(
        root: File,
        titleId: String?,
        category: String = "gd",
        contentId: String? = null
    ): File {
        val content = File(root, "source")
        val metadata = File(content, "sce_sys/param.sfo")
        metadata.parentFile!!.mkdirs()
        val entries = buildList {
            add("CATEGORY" to category)
            add("TITLE" to "Test content")
            if (titleId != null) add("TITLE_ID" to titleId)
            if (contentId != null) add("CONTENT_ID" to contentId)
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
