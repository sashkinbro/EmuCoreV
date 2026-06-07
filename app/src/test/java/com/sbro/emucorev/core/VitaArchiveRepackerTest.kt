package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipOutputStream

class VitaArchiveRepackerTest {
    @get:Rule
    val temp = TemporaryFolder()

    @Test
    fun inspectDetectsInstallMetadataAndVitaminMarker() {
        val archive = temp.newFile("game.vpk")
        writeZip(
            archive,
            "sce_sys/param.sfo" to byteArrayOf(1, 2, 3),
            "sce_module/steroid.suprx" to byteArrayOf(4)
        )

        val inspection = VitaArchiveInspector.inspect(archive.absolutePath)

        assertTrue(inspection.readable)
        assertTrue(inspection.supportedExtension)
        assertTrue(inspection.hasInstallMetadata)
        assertTrue(inspection.vitaminDump)
        assertTrue(inspection.supportedCompression)
    }

    @Test
    fun repackToInstallZipWritesFreshZipWithNormalizedNames() {
        val source = temp.newFile("sample.vpk")
        writeZip(
            source,
            "sce_sys\\param.sfo" to byteArrayOf(1, 2, 3),
            "eboot.bin" to byteArrayOf(4, 5)
        )

        val output = VitaArchiveRepacker.repackToInstallZip(source.absolutePath, temp.root)

        assertNotNull(output)
        assertEquals("zip", output!!.extension)
        ZipFile(output).use { zip ->
            assertNotNull(zip.getEntry("sce_sys/param.sfo"))
            assertNotNull(zip.getEntry("eboot.bin"))
            assertFalse(zip.entries().asSequence().any { it.name.contains('\\') })
        }
    }

    @Test
    fun repackToInstallZipRemovesVitaminMarker() {
        val source = temp.newFile("vitamin.vpk")
        writeZip(
            source,
            "sce_sys/param.sfo" to byteArrayOf(1, 2, 3),
            "eboot.bin" to byteArrayOf(4, 5),
            "sce_module/steroid.suprx" to byteArrayOf(6)
        )

        val output = VitaArchiveRepacker.repackToInstallZip(source.absolutePath, temp.root)

        assertNotNull(output)
        val inspection = VitaArchiveInspector.inspect(output!!.absolutePath)
        assertTrue(inspection.readable)
        assertTrue(inspection.hasInstallMetadata)
        assertFalse(inspection.vitaminDump)
        ZipFile(output).use { zip ->
            assertNotNull(zip.getEntry("sce_sys/param.sfo"))
            assertNotNull(zip.getEntry("eboot.bin"))
            assertNull(zip.getEntry("sce_module/steroid.suprx"))
        }
    }

    private fun writeZip(file: File, vararg entries: Pair<String, ByteArray>) {
        ZipOutputStream(file.outputStream()).use { zip ->
            entries.forEach { (name, bytes) ->
                zip.putNextEntry(ZipEntry(name))
                zip.write(bytes)
                zip.closeEntry()
            }
        }
    }
}
