package com.sbro.emucorev.data

import org.junit.Assert.*
import org.junit.Test

class ProfileDeviceGpuModelTest {
    @Test fun oldDeviceConstructorsKeepAnEmptyOptionalGpuModel() {
        val old = PlayerDevice("id", "device", "maker", "model", "soc", "Mali", 1024,
            "Android", "1", "ECV", true, false, null)
        val oldPublic = PublicPlayerDevice("device", "soc", "Mali", 1024, "Android", "1", "ECV")
        assertEquals("", old.gpuModel)
        assertEquals("", oldPublic.gpuModel)
        val enriched = old.copy(gpuModel = "Xclipse 940")
        assertEquals(old.deviceId, enriched.deviceId)
        assertEquals(old.gpuFamily, enriched.gpuFamily)
        assertEquals(old.soc, enriched.soc)
        assertEquals(old.ramMb, enriched.ramMb)
    }

}
