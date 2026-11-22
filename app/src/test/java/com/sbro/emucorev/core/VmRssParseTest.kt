package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class VmRssParseTest {
    @Test
    fun `parses VmRSS in MiB`() {
        val status = "Name:\temucorev\nVmPeak:\t 9000 kB\nVmRSS:\t 1206272 kB\nThreads:\t78\n"
        assertEquals(1178.0, parseVmRssMib(status)!!, 0.001)
    }

    @Test
    fun `missing or bad VmRSS gives null`() {
        assertNull(parseVmRssMib("Name:\tx\n"))
        assertNull(parseVmRssMib("VmRSS:\t abc kB\n"))
        assertNull(parseVmRssMib("VmRSS:\t 0 kB\n"))
    }
}
