package com.sbro.emucorev.core

import org.junit.Assert.*
import org.junit.Test

class PerformanceHardwareLabelsTest {
    private val device = DeviceGpuInfoResolver.resolve(null, "Snapdragon 8 Gen 2")

    @Test fun `native hardware model is normalized and identified as active evidence`() {
        val label = PerformanceHardwareLabels.gpu("Adreno (TM) 750", device)!!
        assertEquals("Adreno 750", label.text)
        assertEquals(HardwareLabelOrigin.ACTIVE_RENDERER, label.origin)
        assertEquals(GpuInfoSource.NATIVE_RENDERER, label.source)
    }

    @Test fun `unknown native renderer is preserved instead of fabricated from device SoC`() {
        val label = PerformanceHardwareLabels.gpu("  Experimental renderer 9  ", device)!!
        assertEquals("Experimental renderer 9", label.text)
        assertEquals(HardwareLabelOrigin.ACTIVE_RENDERER, label.origin)
        assertEquals(GpuInfoSource.UNKNOWN, label.source)
    }

    @Test fun `software native renderer remains visible even on known hardware`() {
        val label = PerformanceHardwareLabels.gpu("SwiftShader Device", device)!!
        assertEquals("SwiftShader Device", label.text)
        assertEquals(HardwareLabelOrigin.ACTIVE_RENDERER, label.origin)
        assertEquals(GpuInfoSource.UNKNOWN, label.source)
    }

    @Test fun `device fallback is allowed only when active renderer is absent`() {
        for (native in listOf(null, "", "  ")) {
            val label = PerformanceHardwareLabels.gpu(native, device)!!
            assertEquals("Adreno 740", label.text)
            assertEquals(HardwareLabelOrigin.DEVICE_HARDWARE, label.origin)
            assertEquals(GpuInfoSource.SOC_CATALOG, label.source)
        }
    }

    @Test fun `missing device model does not turn device raw software string into hardware`() {
        val unknown = DeviceGpuInfoResolver.resolve("SwiftShader Device", null)
        assertNull(PerformanceHardwareLabels.gpu(null, unknown))
        assertNull(PerformanceHardwareLabels.gpu(null, null))
    }

    @Test fun `CPU label uses bounded explicit identities before later marketing`() {
        assertEquals("Snapdragon 8 Gen 2", PerformanceHardwareLabels.cpu(
            CpuHardwareEvidence(socModel = "SM8550", model = "Snapdragon 8 Gen 3")))
        assertEquals("Allwinner A523", PerformanceHardwareLabels.cpu(
            CpuHardwareEvidence(socModel = "", additionalIdentifiers = listOf("allwinner,a523"))))
        assertEquals("Unknown SoC", PerformanceHardwareLabels.cpu(CpuHardwareEvidence(socModel = "")))
    }
}
