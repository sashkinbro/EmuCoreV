package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class GpuHardwareProfileTest {
    @Test
    fun classifiesKnownGpuAndSocVendors() {
        assertEquals(GpuHardwareProfiles.MALI, GpuHardwareProfiles.classifyHardwareProfile("MediaTek MT6989"))
        assertEquals(GpuHardwareProfiles.ADRENO, GpuHardwareProfiles.classifyHardwareProfile("Qualcomm SM8650"))
        assertEquals(GpuHardwareProfiles.POWERVR, GpuHardwareProfiles.classifyHardwareProfile("IMGTEC PowerVR"))
        assertEquals(GpuHardwareProfiles.MALI, GpuHardwareProfiles.classifyHardwareProfile("Samsung Exynos 2200"))
        assertEquals(GpuHardwareProfiles.MALI, GpuHardwareProfiles.classifyHardwareProfile("ARM Mali-G78"))
    }

    @Test
    fun identifiesMediaTekIndependentlyFromTheGpuFamily() {
        assertTrue(GpuHardwareProfiles.hasMediaTekSocHints("MediaTek MT6989 IMGTEC PowerVR"))
        assertTrue(GpuHardwareProfiles.hasMediaTekSocHints("Dimensity 9300"))
        assertFalse(GpuHardwareProfiles.hasMediaTekSocHints("Samsung Exynos Mali-G78"))
        assertFalse(GpuHardwareProfiles.hasMediaTekSocHints("Qualcomm Snapdragon 8 Gen 3"))
    }
}
