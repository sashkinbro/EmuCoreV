package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Test

class SaveStateFormattingTest {
    @Test
    fun sizeFormattingUsesBinaryUnits() {
        assertEquals("0 B", formatSaveStateSize(0))
        assertEquals("512 B", formatSaveStateSize(512))
        assertEquals("1.0 KB", formatSaveStateSize(1024))
        assertEquals("2.0 KB", formatSaveStateSize(2048))
        assertEquals("1.5 MB", formatSaveStateSize(1024L * 1024L * 3L / 2L))
        assertEquals("2.0 GB", formatSaveStateSize(2L * 1024L * 1024L * 1024L))
    }

    @Test
    fun timestampFormattingIgnoresEmptyValues() {
        assertEquals("", formatSaveStateTimestamp(0))
        assertEquals("", formatSaveStateTimestamp(-1))
    }
}
