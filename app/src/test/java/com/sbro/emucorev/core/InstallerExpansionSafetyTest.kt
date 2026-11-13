package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.apache.commons.compress.archivers.zip.ZipArchiveEntry
import org.apache.commons.compress.archivers.zip.ZipArchiveOutputStream
import org.apache.commons.compress.archivers.zip.ZipFile
import org.junit.Test
import java.io.File
import java.io.IOException
import java.nio.file.Files

class InstallerExpansionSafetyTest {
    @Test
    fun detectsContainerBySignatureWithoutUsingFilename() {
        assertEquals(InstallerArchiveFormat.ZIP, InstallerArchiveSafety.detectContainerFormat(byteArrayOf(0x50, 0x4b, 0x03, 0x04)))
        assertEquals(InstallerArchiveFormat.SEVEN_Z, InstallerArchiveSafety.detectContainerFormat(byteArrayOf(0x37, 0x7a, 0xbc.toByte(), 0xaf.toByte(), 0x27, 0x1c)))
        assertEquals(InstallerArchiveFormat.RAR4, InstallerArchiveSafety.detectContainerFormat(byteArrayOf(0x52, 0x61, 0x72, 0x21, 0x1a, 0x07, 0x00)))
        assertEquals(InstallerArchiveFormat.RAR5, InstallerArchiveSafety.detectContainerFormat(byteArrayOf(0x52, 0x61, 0x72, 0x21, 0x1a, 0x07, 0x01, 0x00)))
        assertNull(InstallerArchiveSafety.detectContainerFormat("not an archive".toByteArray()))
        assertTrue(InstallerArchiveSafety.hasVitaPkgSignature(byteArrayOf(0x7f, 0x50, 0x4b, 0x47)))
        assertFalse(InstallerArchiveSafety.hasVitaPkgSignature(byteArrayOf(0x50, 0x4b, 0x03, 0x04)))
    }

    @Test
    fun normalizesSafeNamesAndRejectsTraversalAbsoluteAndDriveNames() {
        assertEquals("wrapper/sce_sys/param.sfo", InstallerArchiveSafety.normalizeEntryPath("wrapper\\sce_sys\\param.sfo"))
        assertNull(InstallerArchiveSafety.normalizeEntryPath("../outside.pkg"))
        assertNull(InstallerArchiveSafety.normalizeEntryPath("/absolute.pkg"))
        assertNull(InstallerArchiveSafety.normalizeEntryPath("C:\\outside.pkg"))
        assertNull(InstallerArchiveSafety.normalizeEntryPath("\\\\server\\share.pkg"))
    }

    @Test
    fun rejectsLinksAndCaseInsensitiveDuplicateIdentities() {
        val budget = InstallerExpansionBudget(InstallerExpansionLimits(), File(System.getProperty("java.io.tmpdir")))
        budget.enterArchive("outer", depth = 0)
        budget.registerEntry("outer", InstallerArchiveMember("folder/game.vpk", 12, InstallerEntryKind.FILE))
        assertThrows(InstallerExpansionException::class.java) {
            budget.registerEntry("outer", InstallerArchiveMember("FOLDER\\GAME.VPK", 12, InstallerEntryKind.FILE))
        }
        assertThrows(InstallerExpansionException::class.java) {
            budget.registerEntry("outer", InstallerArchiveMember("folder/link", 0, InstallerEntryKind.SYMBOLIC_LINK))
        }
    }

    @Test
    fun readsHostileNamesDuplicatesAndLinksFromARealZipFixture() {
        val archive = Files.createTempFile("hostile-archive-", ".zip").toFile()
        ZipArchiveOutputStream(archive).use { output ->
            for (name in listOf("../escape.bin", "/absolute.bin", "C:\\drive.bin", "safe/../escape.bin")) {
                val entry = ZipArchiveEntry(name)
                output.putArchiveEntry(entry)
                output.write(1)
                output.closeArchiveEntry()
            }
            for (name in listOf("folder/package.vpk", "./folder/package.vpk")) {
                val entry = ZipArchiveEntry(name)
                output.putArchiveEntry(entry)
                output.write(2)
                output.closeArchiveEntry()
            }
            val link = ZipArchiveEntry("folder/link")
            link.unixMode = 0xA1FF
            output.putArchiveEntry(link)
            output.write("../outside".toByteArray())
            output.closeArchiveEntry()
        }

        val budget = InstallerExpansionBudget(InstallerExpansionLimits(), archive.parentFile)
        budget.enterArchive("actual-zip", depth = 0)
        ZipFile.builder().setFile(archive).get().use { zip ->
            zip.entries.asSequence().forEach { entry ->
                val kind = if (entry.isUnixSymlink) InstallerEntryKind.SYMBOLIC_LINK
                else if (entry.isDirectory) InstallerEntryKind.DIRECTORY
                else InstallerEntryKind.FILE
                if (entry.name.contains("..") || entry.name.startsWith("/") || entry.name.startsWith("C:")) {
                    assertNull(InstallerArchiveSafety.normalizeEntryPath(entry.name))
                } else {
                    val member = InstallerArchiveMember(entry.name, entry.size, kind)
                    if (kind == InstallerEntryKind.SYMBOLIC_LINK) {
                        assertThrows(InstallerExpansionException::class.java) { budget.registerEntry("actual-zip", member) }
                    } else {
                        try {
                            budget.registerEntry("actual-zip", member)
                        } catch (error: InstallerExpansionException) {
                            assertEquals(InstallerExpansionFailure.DUPLICATE_ENTRY, error.failure)
                        }
                    }
                }
            }
        }
        archive.delete()
    }

    @Test
    fun boundsDepthEntryCountDeclaredAndActualBytesAndDecoderMemory() {
        val limits = InstallerExpansionLimits(maxNestedDepth = 1, maxArchives = 1, maxEntries = 1, maxExpandedBytes = 8, maxDecoderMemoryBytes = 16)
        val budget = InstallerExpansionBudget(limits, File(System.getProperty("java.io.tmpdir")))
        budget.enterArchive("outer", depth = 0)
        budget.registerEntry("outer", InstallerArchiveMember("small.bin", 8, InstallerEntryKind.FILE))
        budget.recordActualBytes(5)
        assertThrows(InstallerExpansionException::class.java) { budget.recordActualBytes(4) }
        assertThrows(InstallerExpansionException::class.java) { budget.requireDecoderMemory(17) }
        assertThrows(InstallerExpansionException::class.java) { budget.enterArchive(depth = 2) }
        assertThrows(InstallerExpansionException::class.java) { budget.enterArchive("second", depth = 0) }
        assertThrows(InstallerExpansionException::class.java) { budget.enterArchive("outer", depth = 0) }
        assertThrows(InstallerExpansionException::class.java) {
            budget.registerEntry("outer", InstallerArchiveMember("second.bin", 0, InstallerEntryKind.FILE))
        }
    }

    @Test
    fun rejectsWhenTheRequiredFreeSpaceReserveCannotBeMet() {
        val limits = InstallerExpansionLimits(freeSpaceReserveBytes = Long.MAX_VALUE)
        val budget = InstallerExpansionBudget(limits, File(System.getProperty("java.io.tmpdir")))
        budget.enterArchive("outer", depth = 0)
        assertThrows(InstallerExpansionException::class.java) {
            budget.registerEntry("outer", InstallerArchiveMember("small.bin", 0, InstallerEntryKind.FILE))
        }
    }

    @Test
    fun checksCancellationBeforeArchiveWork() {
        val budget = InstallerExpansionBudget(
            InstallerExpansionLimits(),
            File(System.getProperty("java.io.tmpdir")),
            isCancelled = { true }
        )
        assertThrows(InstallerExpansionException::class.java) { budget.enterArchive(depth = 0) }
    }

    @Test
    fun packageSelectionNeverSilentlyPicksFirstCandidate() {
        val candidates = listOf(
            InstallerPackageCandidate("wrapper/z.vpk", InstallerPackageKind.VPK, 200),
            InstallerPackageCandidate("wrapper/a.pkg", InstallerPackageKind.PKG, 100)
        )
        val result = InstallerArchiveSafety.selectPackage(candidates)
        assertTrue(result is InstallerPackageSelection.RequiresChoice)
        assertEquals(listOf("wrapper/a.pkg", "wrapper/z.vpk"), (result as InstallerPackageSelection.RequiresChoice).candidates.map { it.path })
    }

    @Test
    fun cancellationAndSessionCloseRemoveOnlySessionOwnedPartialFiles() {
        val cache = Files.createTempDirectory("expansion-test-").toFile()
        val unrelated = File(cache, "keep.bin").apply { writeText("keep") }
        val session = InstallerExpansionSession.create(cache)
        val committed = session.createAtomicOutput("nested/committed.bin")
        committed.stream.write(byteArrayOf(7, 8, 9))
        assertTrue(committed.commit())
        assertTrue(File(session.root, "nested/committed.bin").readBytes().contentEquals(byteArrayOf(7, 8, 9)))
        val output = session.createAtomicOutput("nested/package.vpk")
        output.stream.write(byteArrayOf(1, 2, 3))
        assertFalse(output.commit(cancelled = { true }))
        assertThrows(IOException::class.java) { output.stream.write(4) }
        session.close()
        assertTrue(unrelated.isFile)
        assertEquals(listOf("keep.bin"), cache.list()?.toList())
        cache.deleteRecursively()
    }
}
