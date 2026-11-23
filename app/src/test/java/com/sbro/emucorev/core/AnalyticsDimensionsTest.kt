package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Test

class AnalyticsDimensionsTest {
    @Test
    fun sanitizersMapUnknownValuesToUnknown() {
        assertEquals("game", AnalyticsDimensions.sanitizeLaunchType("game"))
        assertEquals("unknown", AnalyticsDimensions.sanitizeLaunchType("/sdcard/x"))
        assertEquals("bios_missing", AnalyticsDimensions.sanitizeFailureReason("bios_missing"))
        assertEquals("unknown", AnalyticsDimensions.sanitizeFailureReason("whatever"))
        assertEquals("load", AnalyticsDimensions.sanitizeSaveStateAction("load"))
        assertEquals("unknown", AnalyticsDimensions.sanitizeSaveStateAction("delete"))
    }

    @Test
    fun rendererAndAudioMapToBoundedTokens() {
        assertEquals("vulkan", AnalyticsDimensions.renderer("Vulkan"))
        assertEquals("opengl", AnalyticsDimensions.renderer("OpenGL"))
        assertEquals("unknown", AnalyticsDimensions.renderer(""))
        assertEquals("sdl", AnalyticsDimensions.audioBackend("SDL"))
        assertEquals("cubeb", AnalyticsDimensions.audioBackend("Cubeb"))
        assertEquals("unknown", AnalyticsDimensions.audioBackend("x"))
    }

    @Test
    fun upscaleAndDurationBuckets() {
        assertEquals("native", AnalyticsDimensions.upscale(1f))
        assertEquals("2x", AnalyticsDimensions.upscale(2f))
        assertEquals("3x", AnalyticsDimensions.upscale(3f))
        assertEquals("4x_plus", AnalyticsDimensions.upscale(5f))
        assertEquals("under_1m", AnalyticsDimensions.durationBucket(59_999L))
        assertEquals("1_5m", AnalyticsDimensions.durationBucket(60_000L))
        assertEquals("over_60m", AnalyticsDimensions.durationBucket(3_600_000L))
    }
}
