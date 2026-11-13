package com.sbro.emucorev.core

import androidx.annotation.WorkerThread
import java.io.File
import java.io.InputStream

/** Explicit background-only acquisition; never invoked by the display mapper. */
internal object AndroidSocInfo {
    private const val HINT_BYTES = 4096
    private const val CPUINFO_BYTES = 65_536
    private val propertyKeys = listOf("ro.soc.model", "ro.vendor.soc.model", "ro.board.platform", "ro.hardware")
    private val hintPaths = listOf(
        "/sys/devices/soc0/machine", "/sys/devices/soc0/family",
        "/sys/devices/system/soc/soc0/machine", "/sys/devices/system/soc/soc0/family",
        "/sys/firmware/devicetree/base/compatible", "/sys/firmware/devicetree/base/model"
    )

    /** No implicit lazy read: the future caller must explicitly schedule this off the UI thread. */
    @WorkerThread
    fun readIdentifiers(): List<String> {
        val getter = try {
            Class.forName("android.os.SystemProperties").getMethod("get", String::class.java)
        } catch (_: Exception) {
            null
        }
        return readIdentifiers(
            propertyReader = { key -> getter?.invoke(null, key) as? String },
            openFile = { path -> File(path).inputStream() }
        )
    }

    internal fun readIdentifiers(
        propertyReader: (String) -> String?,
        openFile: (String) -> InputStream
    ): List<String> {
        val identifiers = linkedSetOf<String>()
        fun addHints(value: String?) {
            value?.split('\u0000')?.forEach { part ->
                part.trim().takeIf(String::isNotEmpty)?.let(identifiers::add)
            }
        }
        propertyKeys.forEach { key ->
            val value = try { propertyReader(key) } catch (_: Exception) { null }
            addHints(value)
        }
        hintPaths.forEach { path ->
            addHints(readBounded(path, HINT_BYTES, openFile)?.completeText(nulSeparated = true))
        }
        readBounded("/proc/cpuinfo", CPUINFO_BYTES, openFile)?.completeText(nulSeparated = false)
            ?.lineSequence()?.forEach { line ->
                if (':' in line && line.substringBefore(':').trim().equals("Hardware", ignoreCase = true)) {
                    addHints(line.substringAfter(':'))
                }
            }
        return identifiers.toList()
    }

    private data class BoundedText(val text: String, val reachedLimit: Boolean) {
        fun completeText(nulSeparated: Boolean): String {
            if (!reachedLimit) return text
            // Reaching the exact budget cannot prove EOF. Drop any unterminated
            // final token/line rather than turning SM85500 into a false SM8550.
            val delimiters = if (nulSeparated) charArrayOf('\u0000', '\n', '\r') else charArrayOf('\n', '\r')
            return text.take(text.lastIndexOfAny(delimiters) + 1)
        }
    }

    private fun readBounded(
        path: String,
        limit: Int,
        openFile: (String) -> InputStream
    ): BoundedText? = try {
        openFile(path).use { input ->
            val buffer = ByteArray(limit)
            var used = 0
            while (used < limit) {
                val count = input.read(buffer, used, limit - used)
                if (count < 0) break
                if (count == 0) {
                    val byte = input.read()
                    if (byte < 0) break
                    buffer[used++] = byte.toByte()
                } else used += count
            }
            BoundedText(String(buffer, 0, used, Charsets.UTF_8), used == limit)
        }
    } catch (_: Exception) {
        null
    }
}
