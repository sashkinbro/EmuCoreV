package com.sbro.emucorev.core.vita

import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Reads public component headers, without decrypting or reinstalling firmware. */
internal object FirmwarePackageVersions {
    fun read(file: File): Map<Int, Long> = RandomAccessFile(file, "r").use { input ->
        fun bytes(offset: Long, size: Int): ByteBuffer {
            require(offset >= 0 && size >= 0 && offset <= input.length() - size)
            input.seek(offset)
            return ByteBuffer.wrap(ByteArray(size).also(input::readFully)).order(ByteOrder.LITTLE_ENDIAN)
        }
        val header = bytes(0, 0x80)
        require(header.getInt(0) == 0x55454353 && header.get(4) == 'F'.code.toByte())
        val count = header.getInt(0x18)
        require(count in 1..4096 && 0x80L + count * 32L <= input.length())
        val versions = mutableMapOf<Int, Long>()
        val conflicts = mutableSetOf<Int>()
        repeat(count) { index ->
            val entry = bytes(0x80L + index * 32L, 32)
            val offset = entry.getLong(8)
            val size = entry.getLong(16)
            require(offset >= 0 && size >= 0 && offset <= input.length() - size)
            if (size < 32) return@repeat
            val sce = bytes(offset, 32)
            if (sce.getInt(0) != 0x00454353 || sce.getInt(8) != 0x30040) return@repeat
            val metadata = sce.getLong(16)
            require(metadata >= 32 && metadata <= size - 24)
            val component = bytes(offset + metadata, 24)
            val type = component.getInt(4)
            val version = component.getLong(16)
            if (type !in setOf(1, 10, 23, 24) || version !in 0..0xffffffffL) return@repeat
            if (versions.put(type, version)?.let { it != version } == true) conflicts += type
        }
        versions.filterKeys { it !in conflicts }
    }
}
