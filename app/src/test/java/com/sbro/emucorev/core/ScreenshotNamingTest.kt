package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ScreenshotNamingTest {
    @Test fun sameMillisecondShotsGetDistinctNames() {
        val t = 1_700_000_000_123L
        assertNotEquals(ScreenshotPublisher.displayName(t, 0, "png"), ScreenshotPublisher.displayName(t, 1, "png"))
    }

    @Test fun nameCarriesMillisAndExtension() {
        val name = ScreenshotPublisher.displayName(1_700_000_000_007L, 3, ".JPG")
        assertTrue(name, name.startsWith("EmuCoreV_") && name.endsWith("-007-3.jpg"))
        assertTrue(ScreenshotPublisher.displayName(1L, 0, "").endsWith(".png"))
    }

    @Test fun mimeTypeFollowsExtension() {
        assertEquals("image/jpeg", ScreenshotPublisher.mimeTypeFor("jpg"))
        assertEquals("image/png", ScreenshotPublisher.mimeTypeFor("png"))
    }
}
