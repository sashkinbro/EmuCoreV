package com.sbro.emucorev.core

import org.junit.Assert.*
import org.junit.Test

class GpuDriverLaunchPolicyTest {
    private val adreno = DeviceGpuInfoResolver.resolve(null, "Snapdragon 8 Gen 2")
    private val mali = DeviceGpuInfoResolver.resolve("Mali-G715 MC7", null)
    private fun pkg(name: String = "turnip", vendor: GpuDriverVendor = GpuDriverVendor.ADRENO,
        family: GpuDriverFamily = GpuDriverFamily.TURNIP, gpu: String = "Adreno 6xx/7xx",
        models: List<String> = emptyList()) = InstalledGpuDriver(name, "libvulkan.so", "/unused", false,
        vendor, family, models, GpuDriverFileState.VALID, gpu = gpu)

    @Test fun missingUnknownOrInvalidPackageProjectsSystemWithoutChoosingNeighbor() {
        val valid = pkg()
        assertEquals("", resolveGpuDriverForLaunch("removed", "Vulkan", adreno, listOf(valid)))
        val unknown = DeviceGpuSnapshot(null, null, GpuInfoSource.UNKNOWN)
        assertEquals("", resolveGpuDriverForLaunch(valid.name, "Vulkan", unknown, listOf(valid)))
        assertEquals("", resolveGpuDriverForLaunch(valid.name, "Vulkan", adreno,
            listOf(valid.copy(fileState = GpuDriverFileState.INVALID))))
        assertEquals("", resolveGpuDriverForLaunch("", "Vulkan", adreno, listOf(valid)))
    }

    @Test fun validExactAdrenoPackageCanLaunchFromKnownSocFallbackWithoutGlOrNativeLoadedClaim() {
        val driver = pkg()
        assertEquals(GpuInfoSource.SOC_CATALOG, adreno.source)
        assertEquals(driver.name, resolveGpuDriverForLaunch(driver.name, "Vulkan", adreno, listOf(driver)))
        assertEquals(GpuDriverNativeCapability.UNKNOWN, driver.nativeCapability)
    }

    @Test fun manualTurnipMayAttemptUnverifiedButExplicitNativeUnsupportedDenies() {
        val driver = pkg(gpu = "")
        assertEquals(driver.name, resolveGpuDriverForLaunch(driver.name, "Vulkan", adreno, listOf(driver)))
        val result = GpuDriverCompatibility.resolveSelection(driver.name, null, adreno, listOf(driver))
        assertEquals(GpuDriverCompatibilityState.UNVERIFIED, result.compatibility?.state)
        assertEquals(GpuDriverRuntimeState.NO_SESSION, result.runtimeState)
        assertEquals("", resolveGpuDriverForLaunch(driver.name, "Vulkan", adreno,
            listOf(driver.copy(nativeCapability = GpuDriverNativeCapability.UNSUPPORTED))))
    }

    @Test fun explicitWrongGenerationModelOrVendorStillProjectsSystem() {
        val driver = pkg(gpu = "Adreno 8xx (830/840)")
        for (device in listOf(adreno, DeviceGpuInfoResolver.resolve("Adreno 810", null), mali)) {
            assertEquals("", resolveGpuDriverForLaunch(driver.name, "Vulkan", device, listOf(driver)))
        }
    }

    @Test fun sx2RequiresOwnModelsAndMeasuredGlEvenWhenAnotherVersionIsEligible() {
        val driver = pkg("sx2", GpuDriverVendor.MALI, GpuDriverFamily.MALI_SX2, "Mali", listOf("G715"))
        assertEquals(driver.name, resolveGpuDriverForLaunch(driver.name, "Vulkan", mali, listOf(driver)))
        assertEquals("", resolveGpuDriverForLaunch(driver.name, "Vulkan", mali.copy(source = GpuInfoSource.SOC_CATALOG), listOf(driver)))
        val old = driver.copy(name = "sx2 old", supportedGpuModels = emptyList())
        assertEquals("", resolveGpuDriverForLaunch(old.name, "Vulkan", mali, listOf(old, driver)))
    }

    @Test fun eligiblePanvkOnMaliIsNotBlanketDisabled() {
        val driver = pkg("panvk", GpuDriverVendor.MALI, GpuDriverFamily.PANVK, "Mali Bifrost/Valhall")
        assertEquals(driver.name, resolveGpuDriverForLaunch(driver.name, "Vulkan", mali, listOf(driver)))
    }

    @Test fun openGlAndUnknownBackendKeepVulkanRequestInactiveWithoutMutatingIt() {
        val driver = pkg()
        for (backend in listOf("OpenGL", "unknown")) {
            assertEquals("", resolveGpuDriverForLaunch(driver.name, backend, adreno, listOf(driver)))
        }
        assertEquals("turnip", driver.name)
    }
}
