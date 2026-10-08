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
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.UUID
import javax.crypto.Cipher
import javax.crypto.spec.IvParameterSpec
import javax.crypto.spec.SecretKeySpec

/** Exercises malformed PKG handling through the native installer in a disposable filesystem. */
@RunWith(AndroidJUnit4::class)
class PkgInstallSafetyTest {
    private val context = InstrumentationRegistry.getInstrumentation().targetContext

    @Before
    fun loadNativeLibrary() {
        SDL.setContext(context)
        SDL.loadLibrary("Vita3K", context)
        SDL.setupJNI()
    }

    @Test
    fun invalidMainMagicCannotDeleteInstalledTitleWhenExtensionMagicIsValid() {
        withInstallRoot { root ->
            val sentinel = File(root, "vita/ux0/app/PCSE54321/keep.txt")
            sentinel.parentFile!!.mkdirs()
            sentinel.writeText("installed game")
            val pkg = createPkg(root, mainMagic = 0x01020304)

            val result = install(root, pkg)
            assertTrue("Malformed PKG removed the installed title", sentinel.isFile)
            assertEquals("installed game", sentinel.readText())
            assertFalse("PKG with invalid main magic was accepted", result)
        }
    }

    @Test
    fun truncatedPkgHeaderIsRejectedWithoutChangingInstalledTitle() {
        withInstallRoot { root ->
            val sentinel = File(root, "vita/ux0/app/PCSE54321/keep.txt")
            sentinel.parentFile!!.mkdirs()
            sentinel.writeText("installed game")
            val pkg = File(root, "${UUID.randomUUID()}.pkg")
            val truncated = ByteBuffer.allocate(196).order(ByteOrder.BIG_ENDIAN)
            truncated.putInt(0x01020304)
            truncated.position(192)
            truncated.putInt(0x7F657874)
            pkg.writeBytes(truncated.array())

            val result = install(root, pkg)
            assertTrue(sentinel.isFile)
            assertEquals("installed game", sentinel.readText())
            assertFalse("Truncated PKG was accepted", result)
        }
    }

    @Test
    fun outOfRangeSfoDoesNotRemoveInstalledApps() {
        withInstallRoot { root ->
            val sentinel = File(root, "vita/ux0/app/PCSE54321/keep.txt")
            sentinel.parentFile!!.mkdirs()
            sentinel.writeText("installed game")
            val pkg = createPkg(root, sfoOffsetAtEnd = true, sfoSizeOverride = 4096)

            val result = install(root, pkg)
            assertTrue(sentinel.isFile)
            assertEquals("installed game", sentinel.readText())
            assertFalse("PKG with an out-of-range SFO was accepted", result)
        }
    }

    @Test
    fun unsafeTitleIdDoesNotReplaceOtherContent() {
        withInstallRoot { root ->
            val sentinel = File(root, "vita/ux0/escape/keep.txt")
            sentinel.parentFile!!.mkdirs()
            sentinel.writeText("existing content")
            val pkg = createPkg(root, titleId = "../escape")

            val result = install(root, pkg)
            assertTrue(sentinel.isFile)
            assertEquals("existing content", sentinel.readText())
            assertFalse("PKG with an unsafe title ID was accepted", result)
        }
    }

    @Test
    fun unsafeDlcIdDoesNotOverwriteOtherContent() {
        withInstallRoot { root ->
            val sentinel = File(root, "vita/ux0/escape/data.bin")
            sentinel.parentFile!!.mkdirs()
            sentinel.writeText("existing content")
            val pkg = createPkg(
                root,
                contentType = 0x16,
                contentId = "UP0000-PCSE54321_00-../../escape",
                entryName = "data.bin"
            )

            val result = install(root, pkg)
            assertTrue(sentinel.isFile)
            assertEquals("existing content", sentinel.readText())
            assertFalse("PKG with an unsafe DLC ID was accepted", result)
        }
    }

    @Test
    fun traversalEntryCannotWriteOutsideStagedTitle() {
        withInstallRoot { root ->
            val sentinel = File(root, "vita/ux0/escape/keep.txt")
            sentinel.parentFile!!.mkdirs()
            sentinel.writeText("existing content")
            val pkg = createPkg(root, entryName = "../../escape/keep.txt")

            val result = install(root, pkg)
            assertTrue(sentinel.isFile)
            assertEquals("existing content", sentinel.readText())
            assertFalse("PKG with a traversal entry was accepted", result)
        }
    }

    @Test
    fun outOfRangeEncryptedEntryOffsetsPreserveInstalledTitle() {
        withInstallRoot { root ->
            val sentinel = File(root, "vita/ux0/app/PCSE54321/keep.txt")
            sentinel.parentFile!!.mkdirs()
            sentinel.writeText("installed game")
            val pkg = createPkg(root, entryName = "data.bin", entryNameOffsetOverride = 0xFFFFFFF0L)

            val result = install(root, pkg)
            assertTrue(sentinel.isFile)
            assertEquals("installed game", sentinel.readText())
            assertFalse("PKG with an out-of-range entry name was accepted", result)
        }
    }

    @Test
    fun unalignedEncryptedEntryOffsetsAreRejectedWithoutChangingInstalledTitle() {
        withInstallRoot { root ->
            val sentinel = File(root, "vita/ux0/app/PCSE54321/keep.txt")
            sentinel.parentFile!!.mkdirs()
            sentinel.writeText("installed game")
            val unalignedName = createPkg(root, entryName = "data.bin", entryNameOffsetOverride = 33)

            val nameResult = install(root, unalignedName)
            assertTrue(sentinel.isFile)
            assertEquals("installed game", sentinel.readText())
            assertFalse("PKG with an unaligned encrypted name offset was accepted", nameResult)

            val unalignedData = createPkg(root, entryName = "data.bin", entryDataOffsetOverride = 65)
            val dataResult = install(root, unalignedData)
            assertTrue(sentinel.isFile)
            assertEquals("installed game", sentinel.readText())
            assertFalse("PKG with an unaligned encrypted data offset was accepted", dataResult)

            val unalignedTable = createPkg(root, entryName = "data.bin", tableOffsetOverride = 1)
            val tableResult = install(root, unalignedTable)
            assertTrue(sentinel.isFile)
            assertEquals("installed game", sentinel.readText())
            assertFalse("PKG with an unaligned encrypted table offset was accepted", tableResult)
        }
    }

    @Test
    fun truncatedEncryptedEntryDataPreservesInstalledTitle() {
        withInstallRoot { root ->
            val sentinel = File(root, "vita/ux0/app/PCSE54321/keep.txt")
            sentinel.parentFile!!.mkdirs()
            sentinel.writeText("installed game")
            val pkg = createPkg(root, entryName = "data.bin", truncatePayloadBytes = 5)

            val result = install(root, pkg)
            assertTrue(sentinel.isFile)
            assertEquals("installed game", sentinel.readText())
            assertFalse("PKG with truncated encrypted file data was accepted", result)
        }
    }

    @Test
    fun missingLicenseFailurePreservesInstalledTitleAfterEncryptedEntryExtraction() {
        withInstallRoot { root ->
            val sentinel = File(root, "vita/ux0/app/PCSE54321/keep.txt")
            sentinel.parentFile!!.mkdirs()
            sentinel.writeText("installed game")
            val pkg = createPkg(root, entryName = "sce_sys/package/work.bin")
            val progress = mutableListOf<Float>()
            val appDirectory = File(root, "vita/ux0/app")

            VitaInstallBridge.setListener { event ->
                if (event.stage == "pkg") progress += event.progress
            }
            try {
                val result = install(root, pkg)
                assertTrue("PKG failed before the PFS decrypt phase: $progress", progress.any { it >= 80f })
                assertTrue(sentinel.isFile)
                assertEquals("installed game", sentinel.readText())
                assertFalse("PKG without a license was accepted", result)
                val leftovers = appDirectory.listFiles().orEmpty().filter { it.name.startsWith("PCSE54321.pkg-") }
                assertTrue("PKG staging directories were left behind: $leftovers", leftovers.isEmpty())
            } finally {
                VitaInstallBridge.setListener(null)
            }
        }
    }

    private fun withInstallRoot(test: (File) -> Unit) {
        val root = File(context.cacheDir, "pkg-install-regression-${UUID.randomUUID()}")
        check(root.mkdirs())
        try {
            test(root)
        } finally {
            root.deleteRecursively()
        }
    }

    private fun install(root: File, pkg: File): Boolean {
        val method = VitaInstallBridge::class.java.getDeclaredMethod(
            "nativeInstallPkg",
            String::class.java,
            String::class.java,
            String::class.java,
            String::class.java,
            Int::class.javaPrimitiveType
        ).apply { isAccessible = true }
        return method.invoke(
            VitaInstallBridge,
            File(root, "vita").absolutePath,
            File(root, "cache").absolutePath,
            pkg.absolutePath,
            "",
            1
        ) as Boolean
    }

    private fun createPkg(
        root: File,
        mainMagic: Int = 0x7F504B47,
        contentType: Int = 0x15,
        titleId: String = "PCSE54321",
        contentId: String? = null,
        sfoOffsetAtEnd: Boolean = false,
        sfoSizeOverride: Int? = null,
        entryName: String? = null,
        entryNameOffsetOverride: Long? = null,
        entryDataOffsetOverride: Long? = null,
        tableOffsetOverride: Int = 0,
        truncatePayloadBytes: Int = 0
    ): File {
        val sfo = createSfo(titleId, contentId)
        val infoOffset = 256
        val sfoOffset = infoOffset + 44
        val dataOffset = (sfoOffset + sfo.size + 15) and -16
        val entryData = "synthetic encrypted payload".toByteArray()
        val nameBytes = entryName?.toByteArray()
        val entryNameOffset = 32
        val entryDataOffset = if (nameBytes == null) 64 else (entryNameOffset + nameBytes.size + 15) and -16
        val dataSize = if (entryName == null) 0 else entryDataOffset + entryData.size
        val pkgSize = dataOffset + dataSize
        require(truncatePayloadBytes in 0..entryData.size)
        val actualPkgSize = pkgSize - truncatePayloadBytes
        val actualDataSize = dataSize - truncatePayloadBytes
        val actualSfoOffset = if (sfoOffsetAtEnd) pkgSize - 4 else sfoOffset
        val actualSfoSize = sfoSizeOverride ?: sfo.size
        val pkg = ByteBuffer.allocate(pkgSize).order(ByteOrder.BIG_ENDIAN)

        pkg.putInt(mainMagic)
        pkg.putShort(0.toShort())
        pkg.putShort(0.toShort())
        pkg.putInt(infoOffset)
        pkg.putInt(3)
        pkg.putInt(192)
        pkg.putInt(if (entryName == null) 0 else 1)
        pkg.putLong(actualPkgSize.toLong())
        pkg.putLong(dataOffset.toLong())
        pkg.putLong(actualDataSize.toLong())
        pkg.position(192)
        pkg.putInt(0x7F657874)
        pkg.position(228) // PkgExtHeader.data_type2
        pkg.putInt(2)

        pkg.position(infoOffset)
        pkg.putInt(2).putInt(8).putInt(contentType).putInt(0)
        pkg.putInt(13).putInt(4).putInt(tableOffsetOverride)
        pkg.putInt(14).putInt(8).putInt(actualSfoOffset).putInt(actualSfoSize)
        pkg.position(sfoOffset)
        pkg.put(sfo)

        val packageBytes = pkg.array()
        if (entryName != null && nameBytes != null) {
            val mainKey = SecretKeySpec(aesEcbEncrypt(PKG_KEY_2, ByteArray(16)), "AES")
            val entry = ByteBuffer.allocate(32).order(ByteOrder.BIG_ENDIAN)
                .putInt((entryNameOffsetOverride ?: entryNameOffset.toLong()).toInt())
                .putInt(nameBytes.size)
                .putLong((entryDataOffsetOverride ?: entryDataOffset.toLong()))
                .putLong(entryData.size.toLong())
                .putInt(0)
                .putInt(0)
                .array()
            putAt(packageBytes, dataOffset, cryptCtr(entry, mainKey, ByteArray(16), 0))
            putAt(packageBytes, dataOffset + entryNameOffset, cryptCtr(nameBytes, mainKey, ByteArray(16), entryNameOffset))
            putAt(packageBytes, dataOffset + entryDataOffset, cryptCtr(entryData, mainKey, ByteArray(16), entryDataOffset))
        }

        val output = File(root, "${UUID.randomUUID()}.pkg")
        output.writeBytes(if (truncatePayloadBytes == 0) packageBytes else packageBytes.copyOf(actualPkgSize))
        return output
    }

    private fun createSfo(titleId: String, contentId: String?): ByteArray {
        val entries = buildList {
            add("CATEGORY" to "gd")
            add("TITLE" to "Synthetic package")
            add("TITLE_ID" to titleId)
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
        return sfo.array()
    }

    private fun aesEcbEncrypt(key: ByteArray, input: ByteArray): ByteArray =
        Cipher.getInstance("AES/ECB/NoPadding").run {
            init(Cipher.ENCRYPT_MODE, SecretKeySpec(key, "AES"))
            doFinal(input)
        }

    private fun cryptCtr(input: ByteArray, key: SecretKeySpec, iv: ByteArray, byteOffset: Int): ByteArray {
        val counter = iv.copyOf()
        var amount = (byteOffset / 16).toLong()
        for (index in counter.lastIndex downTo 0) {
            val sum = (counter[index].toInt() and 0xff) + (amount and 0xff).toInt()
            counter[index] = sum.toByte()
            amount = (amount ushr 8) + (sum ushr 8)
        }
        return Cipher.getInstance("AES/CTR/NoPadding").run {
            init(Cipher.ENCRYPT_MODE, key, IvParameterSpec(counter))
            doFinal(input)
        }
    }

    private fun putAt(target: ByteArray, offset: Int, bytes: ByteArray) {
        bytes.copyInto(target, offset)
    }

    companion object {
        private val PKG_KEY_2 = byteArrayOf(
            0xe3.toByte(), 0x1a, 0x70, 0xc9.toByte(), 0xce.toByte(), 0x1d, 0xd7.toByte(), 0x2b,
            0xf3.toByte(), 0xc0.toByte(), 0x62, 0x29, 0x63, 0xf2.toByte(), 0xec.toByte(), 0xcb.toByte()
        )
    }
}
