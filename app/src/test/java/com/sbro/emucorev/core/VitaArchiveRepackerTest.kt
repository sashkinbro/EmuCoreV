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
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.CRC32
import org.apache.commons.compress.compressors.bzip2.BZip2CompressorOutputStream
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
    fun vitaminArchiveRepairPreservesItsRequiredRuntime() {
        val source = temp.newFile("vitamin.vpk")
        writeZip(source, "sce_sys/param.sfo" to byteArrayOf(1, 2, 3),
            "eboot.bin" to byteArrayOf(4, 5), "sce_module/steroid.suprx" to byteArrayOf(6))
        val original = source.readBytes()

        val output = VitaArchiveRepacker.repackToInstallZip(source.absolutePath, temp.root)
        assertNotNull(output)
        ZipFile(output!!).use { zip ->
            org.junit.Assert.assertArrayEquals(byteArrayOf(6),
                zip.getInputStream(zip.getEntry("sce_module/steroid.suprx")).readBytes())
            org.junit.Assert.assertArrayEquals(byteArrayOf(4, 5),
                zip.getInputStream(zip.getEntry("eboot.bin")).readBytes())
        }

        org.junit.Assert.assertArrayEquals(original, source.readBytes())
        assertTrue(VitaArchiveInspector.inspect(source.absolutePath).vitaminDump)
    }

    @Test
    fun nestedCaseVariantVitaminRuntimeCannotBeHiddenByRepair() {
        val source = temp.newFile("nested.vpk")
        writeZip(source, "PCSB00183/sce_sys/param.sfo" to byteArrayOf(1),
            "PCSB00183/./SCE_MODULE\\.\\STEROID.SUPRX" to byteArrayOf(2))
        val output = VitaArchiveRepacker.repackToInstallZip(source.absolutePath, temp.root)
        assertNotNull(output)
        ZipFile(output!!).use { zip ->
            org.junit.Assert.assertArrayEquals(byteArrayOf(2),
                zip.getInputStream(zip.getEntry("PCSB00183/SCE_MODULE/STEROID.SUPRX")).readBytes())
        }
    }

    @Test
    fun inspectionRecognizesNormalizedVitaminRuntimeBeforeInstall() {
        val source = temp.newFile("normalized.vpk")
        writeZip(source, "./PCSB00183/sce_sys/param.sfo" to byteArrayOf(1),
            "PCSB00183/./SCE_MODULE\\.\\STEROID.SUPRX" to byteArrayOf(2))
        assertTrue(VitaArchiveInspector.inspect(source.absolutePath).vitaminDump)
    }

    @Test fun metadataAndVitaminBackupNamesAreOrdinaryAssets() {
        val archive = temp.newFile("backups.vpk")
        writeZip(archive, "assets/sce_sys/param.sfo.bak" to byteArrayOf(1),
            "assets/not_theme.xml" to byteArrayOf(2), "sce_module/steroid.suprx.bak" to byteArrayOf(3))
        val inspection = VitaArchiveInspector.inspect(archive.absolutePath)
        assertFalse(inspection.hasInstallMetadata)
        assertFalse(inspection.vitaminDump)
        val repaired = VitaArchiveRepacker.repackToInstallZip(archive.absolutePath, temp.root)!!
        ZipFile(repaired).use { assertNotNull(it.getEntry("sce_module/steroid.suprx.bak")) }
    }

    @Test fun bzip2NeedsRepairForNativeInstallerAndRepairPreservesItsPayload() {
        val source = temp.newFile("bzip2.vpk")
        val payload = "test metadata payload".toByteArray()
        writeBzip2Zip(source, "sce_sys/param.sfo", payload)
        val inspection = VitaArchiveInspector.inspect(source.absolutePath)
        assertTrue(inspection.readable)
        assertTrue(inspection.hasInstallMetadata)
        assertFalse(inspection.supportedCompression)
        assertEquals(1, inspection.unsupportedCompressionEntries)
        val repaired = VitaArchiveRepacker.repackToInstallZip(source.absolutePath, temp.root)!!
        assertTrue(VitaArchiveInspector.inspect(repaired.absolutePath).supportedCompression)
        ZipFile(repaired).use { zip ->
            org.junit.Assert.assertArrayEquals(payload, zip.getInputStream(zip.getEntry("sce_sys/param.sfo")).readBytes())
        }
    }

    @Test fun corruptedPayloadCannotBeRepackedIntoAnApparentlyValidInstall() {
        val source = temp.newFile("corrupt.vpk")
        val payload = "unique executable bytes".toByteArray()
        ZipOutputStream(source.outputStream()).use { zip ->
            zip.putNextEntry(ZipEntry("eboot.bin").apply {
                method = ZipEntry.STORED
                size = payload.size.toLong()
                compressedSize = size
                crc = CRC32().apply { update(payload) }.value
            })
            zip.write(payload)
            zip.closeEntry()
        }
        val bytes = source.readBytes()
        val offset = bytes.indices.first { start -> start + payload.size <= bytes.size &&
            payload.indices.all { bytes[start + it] == payload[it] } }
        bytes[offset] = (bytes[offset].toInt() xor 1).toByte()
        source.writeBytes(bytes)
        assertNull(VitaArchiveRepacker.repackToInstallZip(source.absolutePath, temp.root))
        assertTrue(File(temp.root, "install_repack_cache").listFiles().orEmpty().isEmpty())
    }

    @Test fun ambiguousCaseVariantMetadataIsRejectedInsteadOfChoosingOneTitle() {
        val source = temp.newFile("ambiguous.vpk")
        writeZip(source, "sce_sys/param.sfo" to byteArrayOf(1), "SCE_SYS/PARAM.SFO" to byteArrayOf(2))
        assertNull(VitaArchiveRepacker.repackToInstallZip(source.absolutePath, temp.root))
        assertTrue(File(temp.root, "install_repack_cache").listFiles().orEmpty().isEmpty())
    }

    @Test fun failedRepairRemovesItsPartialOutputAndKeepsTheOriginal() {
        val source = temp.newFile("unsafe.vpk")
        writeZip(source, "sce_sys/param.sfo" to byteArrayOf(1), "../escape" to byteArrayOf(2))
        val original = source.readBytes()
        assertNull(VitaArchiveRepacker.repackToInstallZip(source.absolutePath, temp.root))
        assertTrue(File(temp.root, "install_repack_cache").listFiles().orEmpty().isEmpty())
        org.junit.Assert.assertArrayEquals(original, source.readBytes())
    }

    @Test fun shortArchiveNameCanBeRepairedAndRepeatedRepairsHaveSeparateOutputs() {
        val source = temp.newFile("a.vpk")
        writeZip(source, "sce_sys/param.sfo" to byteArrayOf(1))
        val first = VitaArchiveRepacker.repackToInstallZip(source.absolutePath, temp.root)
        val second = VitaArchiveRepacker.repackToInstallZip(source.absolutePath, temp.root)
        assertNotNull(first)
        assertNotNull(second)
        assertTrue(first != second && first!!.isFile && second!!.isFile)
    }

    @Test fun explicitRelativeRootDirectoryPreservesCanonicalChildren() {
        val source = temp.newFile("relative-root.vpk")
        writeZip(source, "./" to byteArrayOf(), "./sce_sys/param.sfo" to byteArrayOf(1),
            "./eboot.bin" to byteArrayOf(2))
        val repaired = VitaArchiveRepacker.repackToInstallZip(source.absolutePath, temp.root)
        assertNotNull(repaired)
        ZipFile(repaired!!).use { zip ->
            assertNotNull(zip.getEntry("sce_sys/param.sfo"))
            assertNotNull(zip.getEntry("eboot.bin"))
            assertFalse(zip.entries().asSequence().any { it.name.startsWith("./") })
        }
    }

    private fun writeBzip2Zip(file: File, name: String, payload: ByteArray) {
        val encodedName = name.toByteArray()
        val compressed = ByteArrayOutputStream().also { bytes ->
            BZip2CompressorOutputStream(bytes).use { it.write(payload) }
        }.toByteArray()
        val crc = CRC32().apply { update(payload) }.value.toInt()
        val centralOffset = 30 + encodedName.size + compressed.size
        val centralSize = 46 + encodedName.size
        val buffer = ByteBuffer.allocate(centralOffset + centralSize + 22).order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(0x04034b50).putShort(46).putShort(0).putShort(12).putShort(0).putShort(0)
            .putInt(crc).putInt(compressed.size).putInt(payload.size).putShort(encodedName.size.toShort()).putShort(0)
            .put(encodedName).put(compressed)
        buffer.putInt(0x02014b50).putShort(46).putShort(46).putShort(0).putShort(12).putShort(0).putShort(0)
            .putInt(crc).putInt(compressed.size).putInt(payload.size).putShort(encodedName.size.toShort())
            .putShort(0).putShort(0).putShort(0).putShort(0).putInt(0).putInt(0).put(encodedName)
        buffer.putInt(0x06054b50).putShort(0).putShort(0).putShort(1).putShort(1)
            .putInt(centralSize).putInt(centralOffset).putShort(0)
        file.writeBytes(buffer.array())
    }

    @Test fun repairKeepsOneCopyOfIdenticalRepeatedVitaminRuntime() {
        val source = temp.newFile("duplicate-runtime.vpk")
        val runtime = "identical Vitamin runtime".toByteArray()
        writeDuplicateZip(source, "sce_module/steroid.suprx", runtime, runtime)
        val repaired = VitaArchiveRepacker.repackToInstallZip(source.absolutePath, temp.root)
        assertNotNull(repaired)
        ZipFile(repaired!!).use { zip ->
            assertEquals(1, zip.entries().asSequence().count { it.name == "sce_module/steroid.suprx" })
            org.junit.Assert.assertArrayEquals(runtime, zip.getInputStream(zip.getEntry("sce_module/steroid.suprx")).readBytes())
        }
    }

    @Test fun repairRejectsConflictingRuntimeAndOrdinaryDuplicates() {
        for ((name, first, second) in listOf(
            Triple("sce_module/steroid.suprx", byteArrayOf(1), byteArrayOf(2)),
            Triple("sce_sys/param.sfo", byteArrayOf(1), byteArrayOf(1)),
            Triple("ordinary.bin", byteArrayOf(1), byteArrayOf(1))
        )) {
            val source = temp.newFile("conflict-${name.substringAfterLast('/')}.vpk")
            writeDuplicateZip(source, name, first, second)
            assertNull(VitaArchiveRepacker.repackToInstallZip(source.absolutePath, temp.root))
        }
    }

    private fun writeDuplicateZip(file: File, name: String, first: ByteArray, second: ByteArray) {
        org.apache.commons.compress.archivers.zip.ZipArchiveOutputStream(file).use { zip ->
            for (payload in listOf(first, second)) {
                zip.putArchiveEntry(org.apache.commons.compress.archivers.zip.ZipArchiveEntry(name))
                zip.write(payload)
                zip.closeArchiveEntry()
            }
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
