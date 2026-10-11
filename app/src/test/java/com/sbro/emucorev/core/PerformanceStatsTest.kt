package com.sbro.emucorev.core

import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class PerformanceStatsTest {
    @Test fun cpuUsesElapsedProcessTimeAndAllAvailableCores() {
        val meter = ProcessCpuMeter(4)
        assertNull(meter.sample(1000, 100))
        assertEquals(10.0, meter.sample(2000, 500)!!, 0.0001)
        assertEquals(0.0, meter.sample(3000, 500)!!, 0.0001)
        assertNull(meter.sample(3000, 500))
        assertNull(meter.sample(10, 2))
        assertEquals(25.0, meter.sample(1010, 1002)!!, 0.0001)
    }

    @Test fun kgslCounterUsesReportedIntervalAndRejectsInvalidCounters() {
        assertEquals(25.0, parseKgslBusyPercent(" 250 1000\n")!!, 0.0001)
        assertEquals(0.0, parseKgslBusyPercent("0 1000")!!, 0.0001)
        for (input in listOf("0 0", "1001 1000", "-1 1000", "1", "1 2 3", "no access", "999999999999999999999999 10")) {
            assertNull(input, parseKgslBusyPercent(input))
        }
    }

    @Test fun nativeSnapshotDoesNotInventMissingOrMalformedMeasurements() {
        assertEquals(NativePerformanceStats(), parseNativePerformanceStats("{}"))
        val data = parseNativePerformanceStats("""{"fps":60,"frameMs":17,"gpu":"Adreno 740","backend":"Vulkan","core":"Vita3K 0.2.0","paused":false}""")
        assertEquals(60, data.fps)
        assertEquals(17, data.frameMs)
        assertEquals("Adreno 740", data.gpu)
        val invalid = parseNativePerformanceStats("""{"fps":-1,"frameMs":0,"gpu":"","backend":""}""")
        assertNull(invalid.fps)
        assertNull(invalid.frameMs)
        assertNull(invalid.gpu)
        assertEquals(NativePerformanceStats(), parseNativePerformanceStats("broken"))
    }

    @Test fun gpuModelsUseTheSameShortRendererNamesAsDeviceGpuInfo() {
        assertEquals("Adreno 750", parseGpuModelName("Qualcomm Adreno (TM) 750 Vulkan Driver 1.3.296"))
        assertEquals("Adreno 750", parseGpuModelName("Turnip Adreno (TM) 750"))
        assertEquals("Mali-G720 MC12", parseGpuModelName("Mali-G720 MC12 (Panfrost) Vulkan 1.3"))
        assertEquals("Xclipse 940", parseGpuModelName("Samsung Xclipse 940 Vulkan"))
    }

    @Test fun unknownAndSoftwareRenderersStayUnavailableInsteadOfBeingMisidentified() {
        assertNull(parseGpuModelName("Qualcomm Vulkan Driver 1.3.280"))
        assertNull(parseGpuModelName("Adreno-like 750 driver"))
        assertNull(parseGpuModelName("Turnip SwiftShader Device"))
        assertNull(parseGpuModelName("Android Emulator OpenGL ES Translator"))

        val sample = parseNativePerformanceStats("""{"gpu":"Qualcomm Vulkan Driver 1.3.280"}""")
        assertNull(sample.gpu)
    }

    @Test fun cpuModelUsesTheSharedSocCatalogForMultipleVendorsAndUnknownFallback() {
        assertEquals("Snapdragon 8 Gen 3", MobileSocNameMapper.resolve("SM8650Q"))
        assertEquals("Dimensity 9500", MobileSocNameMapper.resolve("MT6993"))
        assertEquals("Exynos 2600", MobileSocNameMapper.resolve("s5e9965"))
        assertEquals("Google Tensor G5", MobileSocNameMapper.resolve("laguna"))
        assertEquals("Unknown SoC", MobileSocNameMapper.resolve("future_platform"))
    }
}
