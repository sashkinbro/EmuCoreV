package com.sbro.emucorev.core

import org.apache.commons.compress.archivers.zip.ZipFile
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.nio.file.Files
import java.nio.ByteBuffer
import java.nio.ByteOrder

class VitaArchiveContainerSupportTest {
    @Test
    fun findsOnlyValidExtractedVitaContentRoots() {
        val temp = Files.createTempDirectory("expanded-content-").toFile()
        try {
            val valid = File(temp, "wrapper/Game/sce_sys/param.sfo")
            valid.parentFile.mkdirs()
            valid.writeBytes(validSfo("PCSE00001"))
            File(temp, "wrapper/Game/eboot.bin").writeBytes(byteArrayOf(1, 2, 3))
            val invalid = File(temp, "wrapper/Other/sce_sys/param.sfo")
            invalid.parentFile.mkdirs()
            invalid.writeBytes(byteArrayOf(1, 2, 3))

            assertEquals(listOf(File(temp, "wrapper/Game")), VitaArchiveInspector.findInstallableContentRoots(temp))
        } finally {
            temp.deleteRecursively()
        }
    }

    private fun validSfo(titleId: String): ByteArray {
        val keys = "TITLE_ID\u0000CATEGORY\u0000".toByteArray()
        val values = listOf("$titleId\u0000".toByteArray(), "gd\u0000".toByteArray())
        val dataStart = 20 + 2 * 16 + keys.size
        return ByteBuffer.allocate(dataStart + values.sumOf { it.size }).order(ByteOrder.LITTLE_ENDIAN).apply {
            putInt(0x46535000); putInt(0x101); putInt(52); putInt(dataStart); putInt(2)
            var offset = 0
            values.forEachIndexed { index, value ->
                putShort(if (index == 0) 0 else 9); putShort(0x204)
                putInt(value.size); putInt(value.size); putInt(offset)
                offset += value.size
            }
            put(keys); values.forEach { put(it) }
        }.array()
    }

    @Test
    fun repacksNativeExpandedContentWithoutChangingSourceFiles() {
        val temp = Files.createTempDirectory("expanded-repack-").toFile()
        try {
            val root = File(temp, "wrapper/Game")
            File(root, "sce_sys").mkdirs()
            File(root, "sce_sys/param.sfo").writeBytes(ByteArray(64) { it.toByte() })
            val content = File(root, "eboot.bin")
            content.writeBytes(ByteArray(4096) { (it % 251).toByte() })
            val sourceHash = content.readBytes().contentHashCode()

            val archive = VitaArchiveRepacker.repackDirectoryToInstallZip(root, temp, 8192)

            assertTrue(archive?.isFile == true)
            ZipFile.builder().setFile(archive!!).get().use { zip ->
                assertEquals(64L, zip.getEntry("sce_sys/param.sfo").size)
                assertEquals(4096L, zip.getEntry("eboot.bin").size)
            }
            assertEquals(sourceHash, content.readBytes().contentHashCode())
        } finally {
            temp.deleteRecursively()
        }
    }

    @Test
    fun repackRejectsCancellationAndByteBudgetOverflow() {
        val temp = Files.createTempDirectory("expanded-repack-cancel-").toFile()
        try {
            val root = File(temp, "Game").apply { mkdirs() }
            File(root, "eboot.bin").writeBytes(ByteArray(2048))

            assertNull(VitaArchiveRepacker.repackDirectoryToInstallZip(root, temp, 1024))
            assertNull(VitaArchiveRepacker.repackDirectoryToInstallZip(root, temp, 4096, isCancelled = { true }))
            assertTrue(temp.listFiles().orEmpty().none { it.name.endsWith(".zip") || it.name.endsWith(".partial") })
        } finally {
            temp.deleteRecursively()
        }
    }
}
