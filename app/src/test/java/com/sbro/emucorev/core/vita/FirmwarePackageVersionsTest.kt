package com.sbro.emucorev.core.vita

import org.junit.Assert.*
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.file.Files

class FirmwarePackageVersionsTest {
    private fun fixture(vararg components: Pair<Int, Long>, block: (ByteArray) -> Unit) {
        val bytes = ByteArray(0x200 + components.size * 0x100)
        val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        b.putInt(0, 0x55454353); b.put(4, 'F'.code.toByte()); b.putInt(0x18, components.size)
        components.forEachIndexed { index, (type, version) ->
            val offset = 0x200 + index * 0x100
            b.putLong(0x80 + index * 32 + 8, offset.toLong())
            b.putLong(0x80 + index * 32 + 16, 0x100)
            b.putInt(offset, 0x00454353); b.putInt(offset + 8, 0x30040)
            b.putLong(offset + 16, 0x80)
            b.putInt(offset + 0x84, type); b.putLong(offset + 0x90, version)
        }
        block(bytes)
    }

    private fun read(bytes: ByteArray): Map<Int, Long> {
        val file = Files.createTempFile("firmware-versions", ".pup").toFile()
        return try { file.writeBytes(bytes); FirmwarePackageVersions.read(file) } finally { file.delete() }
    }

    @Test fun readsComponentVersionsIndependentlyOfFirmwareVersion() = fixture(1 to 0x3740000L, 23 to 0x1000010L) {
        assertEquals(mapOf(1 to 0x3740000L, 23 to 0x1000010L), read(it))
    }

    @Test fun conflictingPartsAndUnrelatedPackagesDoNotCreateMetadata() = fixture(23 to 1L, 23 to 2L, 7 to 9L, 10 to 3L) {
        assertEquals(mapOf(10 to 3L), read(it))
    }

    @Test fun rejectsOutOfBoundsComponentBeforeReading() = fixture(23 to 1L) {
        ByteBuffer.wrap(it).order(ByteOrder.LITTLE_ENDIAN).putLong(0x88, Long.MAX_VALUE)
        assertThrows(IllegalArgumentException::class.java) { read(it) }
    }
}
