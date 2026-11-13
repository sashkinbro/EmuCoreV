package com.sbro.emucorev.core

import java.io.ByteArrayInputStream
import java.io.IOException
import java.io.InputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class AndroidSocInfoTest {
    private val properties = listOf("ro.soc.model", "ro.vendor.soc.model", "ro.board.platform", "ro.hardware")
    private val paths = listOf(
        "/sys/devices/soc0/machine", "/sys/devices/soc0/family",
        "/sys/devices/system/soc/soc0/machine", "/sys/devices/system/soc/soc0/family",
        "/sys/firmware/devicetree/base/compatible", "/sys/firmware/devicetree/base/model"
    )
    private val cpuinfo = "/proc/cpuinfo"
    private fun stream(text: String) = ByteArrayInputStream(text.toByteArray(Charsets.UTF_8))

    @Test fun preservesSourcePriorityAndDeduplicatesTrimmedHints() {
        val propertyReads = mutableListOf<String>()
        val fileReads = mutableListOf<String>()
        val result = AndroidSocInfo.readIdentifiers(
            propertyReader = { key -> propertyReads += key; " ${properties.indexOf(key)} " },
            openFile = { path ->
                fileReads += path
                if (path == cpuinfo) stream("Hardware : 0\nHardware : cpu-platform\n")
                else stream(" file-${paths.indexOf(path)} \u0000 0 \u0000")
            }
        )
        assertEquals(properties, propertyReads)
        assertEquals(paths + cpuinfo, fileReads)
        assertEquals(listOf("0", "1", "2", "3") + (0..5).map { "file-$it" } + "cpu-platform", result)
    }

    @Test fun deniedSourcesDoNotDiscardLaterIdentifiers() {
        val result = AndroidSocInfo.readIdentifiers(
            propertyReader = { key -> if (key == properties.first()) throw SecurityException("fixture") else if (key == properties[1]) "SM8750" else "" },
            openFile = { path -> when (path) {
                paths.first() -> throw IOException("fixture")
                paths[1] -> stream("A523")
                cpuinfo -> stream("Hardware: MT6789")
                else -> throw SecurityException("fixture")
            } }
        )
        assertEquals(listOf("SM8750", "A523", "MT6789"), result)
    }

    @Test fun boundsEveryFileReadAndClosesAllStreams() {
        class CountingStream : InputStream() {
            var consumed = 0
            var closed = false
            override fun read(): Int { consumed++; return 'x'.code }
            override fun read(bytes: ByteArray, offset: Int, length: Int): Int {
                bytes.fill('x'.code.toByte(), offset, offset + length)
                consumed += length
                return length
            }
            override fun close() { closed = true }
        }
        val opened = linkedMapOf<String, CountingStream>()
        val result = AndroidSocInfo.readIdentifiers({ null }) { path -> CountingStream().also { opened[path] = it } }
        assertEquals(paths + cpuinfo, opened.keys.toList())
        paths.forEach { assertEquals(it, 4096, opened.getValue(it).consumed) }
        assertEquals(65_536, opened.getValue(cpuinfo).consumed)
        assertTrue(opened.values.all { it.closed })
        assertTrue("A budget-clipped token cannot become a chip identifier", result.isEmpty())
    }

    @Test fun completesShortReadsAndClosesStreamsThatFailMidRead() {
        var shortClosed = false
        var failedClosed = false
        val short = object : ByteArrayInputStream("SM8650".toByteArray()) {
            override fun read(bytes: ByteArray, offset: Int, length: Int): Int = super.read(bytes, offset, minOf(1, length))
            override fun close() { shortClosed = true; super.close() }
        }
        val failing = object : InputStream() {
            override fun read(): Int = throw IOException("fixture")
            override fun close() { failedClosed = true }
        }
        val result = AndroidSocInfo.readIdentifiers({ null }) { path -> when (path) {
            paths.first() -> short
            paths[1] -> failing
            else -> stream("")
        } }
        assertEquals(listOf("SM8650"), result)
        assertTrue(shortClosed)
        assertTrue(failedClosed)
    }

    @Test fun cpuinfoAcceptsOnlyActualHardwareKeys() {
        val result = AndroidSocInfo.readIdentifiers({ null }) { path ->
            if (path != cpuinfo) stream("") else stream("""
                Processor: ignored processor description
                Serial: ignored fixture serial
                Hardware model: ignored field
                HARDWARE : qcom,sm8750
                  hardware: allwinner,sun55i-a523
                Hardware missing colon
            """.trimIndent())
        }
        assertEquals(listOf("qcom,sm8750", "allwinner,sun55i-a523"), result)
    }

    @Test fun deviceTreeNulValuesRetainOrderWithoutBlankOrDuplicateEntries() {
        val result = AndroidSocInfo.readIdentifiers({ null }) { path -> when (path) {
            paths[4] -> stream("\u0000 vendor,board \u0000allwinner,sun55iw3\u0000\u0000")
            paths[5] -> stream("allwinner,sun55iw3\u0000 TG5050 \u0000")
            else -> stream("")
        } }
        assertEquals(listOf("vendor,board", "allwinner,sun55iw3", "TG5050"), result)
    }

    @Test fun clippedFinalTokensAndHardwareLinesAreDiscarded() {
        val complete = "vendor,complete\u0000"
        val dt = complete + " ".repeat(4096 - complete.length - 6) + "SM85500"
        val fullLine = "Hardware: valid-platform\n"
        val proc = fullLine + " ".repeat(65_536 - fullLine.length - "Hardware: SM8550".length) + "Hardware: SM85500"
        val result = AndroidSocInfo.readIdentifiers({ null }) { path -> when (path) {
            paths[4] -> stream(dt)
            cpuinfo -> stream(proc)
            else -> stream("")
        } }
        assertEquals(listOf("vendor,complete", "valid-platform"), result)
    }

    @Test fun allPermissionFailuresAreNonfatalAndEverySourceIsAttempted() {
        var propertyReads = 0
        var fileReads = 0
        val result = AndroidSocInfo.readIdentifiers(
            { propertyReads++; throw SecurityException("fixture") },
            { fileReads++; throw IOException("fixture") }
        )
        assertTrue(result.isEmpty())
        assertEquals(4, propertyReads)
        assertEquals(7, fileReads)
    }
}
