package com.sbro.emucorev.data

import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.Assert.*
import org.junit.Test

class TrophyProgressArchiveTest {
    private fun progress(vararg ids: Int, timestamp: Long = 100): ByteArray {
        val bytes = ByteArray(1648)
        val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(0, 0x12D5819A)
        buffer.putInt(36, 1)
        buffer.putInt(40, 128)
        buffer.putInt(44, -1)
        ids.forEach { id ->
            val offset = 4 + id / 32 * 4
            buffer.putInt(offset, buffer.getInt(offset) or (1 shl (id % 32)))
            buffer.putLong(112 + id * 8, timestamp)
            buffer.putInt(1136 + id * 4, 4)
        }
        return bytes
    }

    @Test fun allGamesRoundTripInOneDeterministicArchive() {
        val games = mapOf("NPWR00002_00" to progress(3), "NPWR00001_00" to progress(0, 127))
        val packed = TrophyProgressArchive.encode(games)
        val restored = TrophyProgressArchive.decode(packed)
        assertEquals(games.keys, restored.keys)
        games.forEach { (id, bytes) -> assertArrayEquals(bytes, restored.getValue(id)) }
        assertArrayEquals(packed, TrophyProgressArchive.encode(games.toList().reversed().toMap()))
        assertTrue(packed.size < games.values.sumOf { it.size })
    }

    @Test fun devicesMergeUnlocksWithoutResettingProgress() {
        val merged = TrophyProgressArchive.merge(
            mapOf("NPWR00001_00" to progress(0, 5, timestamp = 200)),
            mapOf("NPWR00001_00" to progress(0, 127, timestamp = 100))
        ).getValue("NPWR00001_00")
        val buffer = ByteBuffer.wrap(merged).order(ByteOrder.LITTLE_ENDIAN)
        assertEquals(33, buffer.getInt(4))
        assertEquals(Int.MIN_VALUE, buffer.getInt(16))
        assertEquals(100, buffer.getLong(112))
        assertEquals(200, buffer.getLong(112 + 5 * 8))
        assertArrayEquals(merged, TrophyProgressArchive.merge(mapOf("NPWR00001_00" to merged),
            mapOf("NPWR00001_00" to progress(0, 127, timestamp = 100))).getValue("NPWR00001_00"))
    }

    @Test fun missingLocalGamesAreRetained() {
        val merged = TrophyProgressArchive.merge(mapOf("NPWR00001_00" to progress(0)),
            mapOf("NPWR00002_00" to progress(1)))
        assertEquals(2, merged.size)
    }

    @Test fun invalidAndUnsafePayloadsAreRejected() {
        assertThrows(IllegalArgumentException::class.java) {
            TrophyProgressArchive.encode(mapOf("../escape" to progress(0)))
        }
        assertThrows(IllegalArgumentException::class.java) {
            TrophyProgressArchive.encode(mapOf("NPWR00001_00" to ByteArray(1648)))
        }
        val output = ByteArrayOutputStream()
        ZipOutputStream(output).use { zip ->
            zip.putNextEntry(ZipEntry("../TROPUSR.DAT")); zip.write(progress(0)); zip.closeEntry()
        }
        assertThrows(IllegalArgumentException::class.java) { TrophyProgressArchive.decode(output.toByteArray()) }
        assertThrows(Exception::class.java) { TrophyProgressArchive.decode(byteArrayOf(1, 2, 3)) }
    }
}
