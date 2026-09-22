package com.sbro.emucorev.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class SaveArchiveLayoutTest {
    @get:Rule
    val temp = TemporaryFolder()

    @Test
    fun locatesSavedataInsideVitaFilesystemArchive() {
        val save = temp.newFolder("ux0", "user", "00", "savedata", "PCSE00001")
        writeFile(File(save, "sce_sys/param.sfo"))

        val located = SaveArchiveLayout.locateSavedataDirectories(temp.root)

        assertEquals(listOf("PCSE00001"), located.map(File::getName))
    }

    @Test
    fun locatesSavedataWithoutUx0Prefix() {
        val save = temp.newFolder("user", "00", "savedata", "PCSB00002")
        writeFile(File(save, "data.bin"))

        val located = SaveArchiveLayout.locateSavedataDirectories(temp.root)

        assertEquals(listOf("PCSB00002"), located.map(File::getName))
    }

    @Test
    fun locatesTopLevelSavedataFolder() {
        val save = temp.newFolder("savedata", "PCSE00003")
        writeFile(File(save, "data.bin"))

        val located = SaveArchiveLayout.locateSavedataDirectories(temp.root)

        assertEquals(listOf("PCSE00003"), located.map(File::getName))
    }

    @Test
    fun locatesMultipleSavesAndSkipsEmptyDirectories() {
        writeFile(File(temp.newFolder("ux0", "user", "00", "savedata", "PCSE00001"), "data.bin"))
        writeFile(File(temp.newFolder("ux0", "user", "00", "savedata", "PCSB00002"), "data.bin"))
        temp.newFolder("ux0", "user", "00", "savedata", "EMPTY0001")

        val located = SaveArchiveLayout.locateSavedataDirectories(temp.root)

        assertEquals(setOf("PCSE00001", "PCSB00002"), located.map(File::getName).toSet())
    }

    @Test
    fun ignoresPlainSaveManagerExport() {
        writeFile(File(temp.newFolder("PCSE00001", "sce_sys"), "param.sfo"))

        assertTrue(SaveArchiveLayout.locateSavedataDirectories(temp.root).isEmpty())
    }

    private fun writeFile(file: File) {
        file.parentFile?.mkdirs()
        file.writeBytes(byteArrayOf(1, 2, 3))
    }
}
