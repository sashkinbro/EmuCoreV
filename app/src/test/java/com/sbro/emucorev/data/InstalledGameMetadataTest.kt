package com.sbro.emucorev.data

import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

class InstalledGameMetadataTest {
    @get:Rule val temporaryFolder = TemporaryFolder()

    @Test
    fun usesInstalledUpdateVersionForCheatMatching() {
        val ux0 = temporaryFolder.newFolder("ux0")
        val app = File(ux0, "app/PCSE00001")
        writeSfo(app, "PCSE00001", "01.00")
        writeSfo(File(ux0, "patch/PCSE00001"), "PCSE00001", "01.02")

        val metadata = readInstalledGameMetadata(app)
        assertEquals("01.02", metadata.version)
        assertEquals("PCSE00001", metadata.titleId)
    }

    @Test
    fun ignoresUpdateMetadataForAnotherTitle() {
        val ux0 = temporaryFolder.newFolder("ux0")
        val app = File(ux0, "app/PCSE00001")
        writeSfo(app, "PCSE00001", "01.00")
        writeSfo(File(ux0, "patch/PCSE00001"), "PCSB00001", "01.02")
        assertEquals("01.00", readInstalledGameMetadata(app).version)
    }

    @Test
    fun fallsBackToBaseVersionWhenUpdateSfoIsMalformed() {
        val ux0 = temporaryFolder.newFolder("ux0")
        val app = File(ux0, "app/PCSE00001")
        writeSfo(app, "PCSE00001", "01.00")
        val malformed = File(ux0, "patch/PCSE00001/sce_sys/param.sfo")
        malformed.parentFile!!.mkdirs()
        malformed.writeBytes(ByteBuffer.allocate(20).order(ByteOrder.LITTLE_ENDIAN)
            .putInt(0x46535000).putInt(0x101).putInt(20).putInt(20).putInt(Int.MAX_VALUE).array())
        assertEquals("01.00", readInstalledGameMetadata(app).version)
    }

    private fun writeSfo(directory: File, titleId: String, version: String) {
        val file = File(directory, "sce_sys/param.sfo")
        file.parentFile!!.mkdirs()
        val keys = listOf("TITLE_ID\u0000", "APP_VER\u0000").map { it.toByteArray() }
        val values = listOf("$titleId\u0000", "$version\u0000").map { it.toByteArray() }
        val keyStart = 20 + keys.size * 16
        val dataStart = keyStart + keys.sumOf { it.size }
        val buffer = ByteBuffer.allocate(dataStart + values.sumOf { it.size }).order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(0x46535000).putInt(0x101).putInt(keyStart).putInt(dataStart).putInt(keys.size)
        var keyOffset = 0
        var dataOffset = 0
        for (index in keys.indices) {
            buffer.putShort(keyOffset.toShort()).putShort(0x0204)
                .putInt(values[index].size).putInt(values[index].size).putInt(dataOffset)
            keyOffset += keys[index].size
            dataOffset += values[index].size
        }
        keys.forEach { buffer.put(it) }
        values.forEach { buffer.put(it) }
        file.writeBytes(buffer.array())
    }
}
