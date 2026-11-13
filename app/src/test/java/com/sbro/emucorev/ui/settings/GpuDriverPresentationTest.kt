package com.sbro.emucorev.ui.settings

import com.sbro.emucorev.R
import com.sbro.emucorev.core.*
import org.junit.Assert.*
import org.junit.Test

class GpuDriverPresentationTest {
    private val device = DeviceGpuSnapshot(GpuModelInfo(GpuVendor.MALI, "Mali-G715 MC7"), "Mali-G715 MC7", GpuInfoSource.GL_RENDERER)
    private val pkg = GpuDriverPackageEvidence("PanVK exact", GpuVendor.MALI, GpuDriverFamily.PANVK,
        fileState = GpuDriverFileState.VALID, nativeCapability = GpuDriverNativeCapability.UNKNOWN)
    private fun selection(global: String = pkg.name, game: String? = null,
                          packages: List<GpuDriverPackageEvidence> = listOf(pkg)) =
        GpuDriverSelectionPolicy.resolve(global, game, device, packages)

    @Test fun removedGlobalSelectionRemainsNamedInsteadOfSystem() {
        val result = selection(packages = emptyList())
        val text = gpuDriverSelectionText(result, "Vulkan", false)
        assertEquals(R.string.settings_gpu_driver_status_missing_selection, text.titleRes)
        assertEquals(pkg.name, text.titleArgument)
        assertNull(text.detailRes)
        assertEquals(pkg.name, result.requestedName)
        assertEquals(GpuDriverRuntimeState.NO_SESSION, result.runtimeState)
    }

    @Test fun inheritedMissingSelectionReportsUnavailableRatherThanHealthyGlobal() {
        val text = gpuDriverSelectionText(selection(packages = emptyList()), "Vulkan", true)
        assertEquals(R.string.settings_gpu_driver_status_missing_selection, text.titleRes)
        assertEquals(pkg.name, text.titleArgument)
    }

    @Test fun gameInheritAndExplicitSystemRetainDifferentLabels() {
        val inherited = gpuDriverSelectionText(selection(global = ""), "Vulkan", true)
        val explicit = gpuDriverSelectionText(selection(game = ""), "Vulkan", true)
        assertEquals(R.string.settings_gpu_driver_status_global_system, inherited.titleRes)
        assertEquals(R.string.settings_gpu_driver_status_game_system, explicit.titleRes)
        assertNull(inherited.titleArgument)
        assertNull(explicit.titleArgument)
    }

    @Test fun eligibleNativeUnknownIsSelectedAndUnverifiedNeverLoaded() {
        val result = selection()
        val text = gpuDriverSelectionText(result, "Vulkan", false)
        assertTrue(result.requestable)
        assertEquals(R.string.settings_gpu_driver_status_active, text.titleRes)
        assertEquals(R.string.settings_gpu_driver_status_native_unverified, text.detailRes)
        assertEquals(GpuDriverRuntimeState.NO_SESSION, result.runtimeState)
        assertNull(result.activeCustomName)
    }

    @Test fun inheritedIncompatibilityIsVisibleAlongsideInheritanceLabel() {
        val result = selection(packages = listOf(pkg.copy(vendor = GpuVendor.ADRENO, family = GpuDriverFamily.TURNIP)))
        val text = gpuDriverSelectionText(result, "Vulkan", true)
        assertEquals(R.string.settings_gpu_driver_status_global, text.titleRes)
        assertEquals(R.string.settings_gpu_driver_status_incompatible_selection, text.detailRes)
        assertFalse(result.requestable)
    }

    @Test fun invalidFilesAndUnknownMetadataRemainDistinct() {
        val invalid = gpuDriverSelectionText(selection(packages = listOf(pkg.copy(fileState = GpuDriverFileState.INVALID))), "Vulkan", false)
        val unknown = gpuDriverSelectionText(selection(packages = listOf(pkg.copy(family = GpuDriverFamily.OTHER))), "Vulkan", false)
        assertEquals(R.string.settings_gpu_driver_status_invalid_selection, invalid.detailRes)
        assertEquals(R.string.settings_gpu_driver_status_unverified_selection, unknown.detailRes)
    }

    @Test fun openGlKeepsRequestedCustomButShowsVulkanOnlyContext() {
        val result = selection()
        val text = gpuDriverSelectionText(result, "OpenGL", true)
        assertEquals(R.string.settings_gpu_driver_status_global, text.titleRes)
        assertEquals(pkg.name, text.titleArgument)
        assertEquals(R.string.settings_gpu_driver_status_renderer_desc, text.detailRes)
        assertEquals(GpuDriverRuntimeState.NO_SESSION, result.runtimeState)
    }
    @Test fun requestableManualTurnipStillShowsUnverifiedHardwareRatherThanLoaded() {
        // The adapter permits a manual attempt when version requirements are unavailable.
        val result = selection(global = "Manual Turnip").copy(
            compatibility = GpuDriverSelectionCompatibility(GpuDriverCompatibilityState.UNVERIFIED,
                GpuDriverCompatibilityReason.PACKAGE_REQUIREMENTS_UNVERIFIED),
            decision = GpuDriverSelectionDecision.UNVERIFIED_HARDWARE,
            requestable = true,
            nextLaunchName = "Manual Turnip"
        )
        val text = gpuDriverSelectionText(result, "Vulkan", false)
        assertTrue(result.requestable)
        assertEquals(R.string.settings_gpu_driver_status_active, text.titleRes)
        assertEquals("Manual Turnip", text.titleArgument)
        assertEquals(R.string.settings_gpu_driver_status_unverified_selection, text.detailRes)
        assertEquals(GpuDriverRuntimeState.NO_SESSION, result.runtimeState)
        assertNull(result.activeCustomName)
    }

}
