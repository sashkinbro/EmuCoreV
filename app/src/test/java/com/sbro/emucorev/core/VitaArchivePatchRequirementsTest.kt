package com.sbro.emucorev.core

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class VitaArchivePatchRequirementsTest {
    @get:Rule val temp = TemporaryFolder()

    @Test fun identifiesUpdateTitleFromRootAndNestedMetadata() {
        for (prefix in listOf("", "bundle/")) {
            val archive = temp.newFile("update-${prefix.length}.vpk")
            writeArchive(archive, prefix + "sce_sys/param.sfo", metadata("PCSE00383", "gp"))
            assertEquals(setOf("PCSE00383"), VitaArchiveInspector.inspect(archive.path).patchTitleIds)
        }
    }

    @Test fun ordinaryGameAndMalformedOrUnsafeMetadataDoNotSuggestMissingBase() {
        for ((index, data) in listOf(metadata("PCSE00383", "gd"), byteArrayOf(1, 2), metadata("../other", "gp")).withIndex()) {
            val archive = temp.newFile("ordinary-$index.vpk")
            writeArchive(archive, "sce_sys/param.sfo", data)
            val inspection = VitaArchiveInspector.inspect(archive.path)
            assertTrue(inspection.readable)
            assertTrue(inspection.patchTitleIds.isEmpty())
        }
    }

    private fun metadata(titleId: String, category: String): ByteArray {
        val keys = "TITLE_ID\u0000CATEGORY\u0000".toByteArray()
        val values = listOf(titleId, category).map { (it + '\u0000').toByteArray() }
        val start = 20 + 2 * 16 + keys.size
        return ByteBuffer.allocate(start + values.sumOf { it.size }).order(ByteOrder.LITTLE_ENDIAN).apply {
            putInt(0x46535000); putInt(0x101); putInt(52); putInt(start); putInt(2)
            var offset = 0
            values.forEachIndexed { index, value ->
                putShort(if (index == 0) 0 else 9); putShort(0x204)
                putInt(value.size); putInt(value.size); putInt(offset)
                offset += value.size
            }
            put(keys); values.forEach { put(it) }
        }.array()
    }

    private fun writeArchive(file: File, name: String, data: ByteArray) {
        ZipOutputStream(file.outputStream()).use { zip ->
            zip.putNextEntry(ZipEntry(name)); zip.write(data); zip.closeEntry()
        }
    }
}
