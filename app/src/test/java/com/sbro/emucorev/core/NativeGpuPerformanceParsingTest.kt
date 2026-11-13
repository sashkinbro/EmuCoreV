package com.sbro.emucorev.core

import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(manifest = Config.NONE, sdk = [29])
class NativeGpuPerformanceParsingTest {
    @Test fun unknownAndSoftwareNativeNamesRemainActiveRendererEvidence() {
        assertEquals("Qualcomm Vulkan Driver 1.3.280", parseNativePerformanceStats("""{"gpu":"  Qualcomm Vulkan Driver 1.3.280  "}""").gpu)
        assertEquals("SwiftShader Device", parseNativePerformanceStats("""{"gpu":"SwiftShader Device"}""").gpu)
        assertEquals("llvmpipe", parseNativePerformanceStats("""{"gpu":"llvmpipe"}""").gpu)
    }

    @Test fun knownNativeNamesAreCanonicalAndMissingNamesRemainMissing() {
        assertEquals("Adreno 750", parseNativePerformanceStats("""{"gpu":"Turnip Adreno (TM) 750"}""").gpu)
        assertNull(parseNativePerformanceStats("""{"gpu":"  "}""").gpu)
        assertNull(parseNativePerformanceStats("""{"fps":60}""").gpu)
        assertNull(parseNativePerformanceStats("""{"gpu":123}""").gpu)
    }
}
