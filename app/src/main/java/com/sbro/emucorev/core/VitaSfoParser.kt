package com.sbro.emucorev.core

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

data class VitaSfoData(
    val titleId: String? = null,
    val title: String? = null,
    val version: String? = null,
    val category: String? = null,
    val contentId: String? = null
)

object VitaSfoParser {
    fun parse(file: File): VitaSfoData {
        if (!file.exists() || !file.isFile) return VitaSfoData()
        val bytes = runCatching { file.readBytes() }.getOrElse { return VitaSfoData() }
        if (bytes.size < 20) return VitaSfoData()

        val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        if (buffer.int != 0x46535000) return VitaSfoData()

        buffer.short
        buffer.short
        val keyTableStart = buffer.int
        val dataTableStart = buffer.int
        val entryCount = buffer.int
        val values = mutableMapOf<String, String>()

        repeat(entryCount) {
            val keyOffset = buffer.short.toInt() and 0xFFFF
            buffer.get()
            buffer.get()
            val valueLength = buffer.int
            buffer.int
            val dataOffset = buffer.int
            val key = readCString(bytes, keyTableStart + keyOffset)
            val valueStart = dataTableStart + dataOffset
            if (valueStart !in bytes.indices) return@repeat
            val valueEnd = (valueStart + valueLength).coerceAtMost(bytes.size)
            val value = bytes.copyOfRange(valueStart, valueEnd).decodeToString()
                .replace("\u0000", "")
                .trim()
            if (key.isNotBlank() && value.isNotBlank()) {
                values[key] = value
            }
        }

        return VitaSfoData(
            titleId = values["TITLE_ID"],
            title = values["TITLE"],
            version = values["APP_VER"],
            category = values["CATEGORY"],
            contentId = values["CONTENT_ID"]
        )
    }

    private fun readCString(bytes: ByteArray, start: Int): String {
        if (start !in bytes.indices) return ""
        var end = start
        while (end < bytes.size && bytes[end].toInt() != 0) {
            end++
        }
        return bytes.copyOfRange(start, end).decodeToString()
    }
}
