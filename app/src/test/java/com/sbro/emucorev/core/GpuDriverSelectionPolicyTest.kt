package com.sbro.emucorev.core

import org.junit.Assert.*
import org.junit.Test

class GpuDriverSelectionPolicyTest {
    private val mali = device(GpuVendor.MALI, "Mali-G715 MC7")
    private val adreno = device(GpuVendor.ADRENO, "Adreno 740")
    private val panvk = driver("PanVK 25.1", GpuVendor.MALI, GpuDriverFamily.PANVK)
    private val turnip = driver("Turnip 25.1", GpuVendor.ADRENO, GpuDriverFamily.TURNIP)

    private fun device(vendor: GpuVendor, name: String, source: GpuInfoSource = GpuInfoSource.GL_RENDERER) =
        DeviceGpuSnapshot(GpuModelInfo(vendor, name), name, source)

    private fun driver(
        name: String, vendor: GpuVendor, family: GpuDriverFamily,
        models: Set<String> = emptySet(),
        native: GpuDriverNativeCapability = GpuDriverNativeCapability.SUPPORTED,
        file: GpuDriverFileState = GpuDriverFileState.VALID
    ) = GpuDriverPackageEvidence(name, vendor, family, models, file, native)

    private fun resolve(
        global: String = panvk.name, override: String? = null,
        device: DeviceGpuSnapshot = mali,
        packages: List<GpuDriverPackageEvidence> = listOf(panvk, turnip),
        runtime: GpuDriverRuntimeEvidence? = null
    ) = GpuDriverSelectionPolicy.resolve(global, override, device, packages, runtime)

    private fun compatibility(device: DeviceGpuSnapshot, driver: GpuDriverPackageEvidence) =
        GpuDriverSelectionPolicy.compatibility(device, driver)

    @Test fun inheritedCustomPreservesExactDirectoryName() {
        val named = panvk.copy(name = "  PanVK custom v2  ")
        val result = resolve(global = named.name, packages = listOf(named))
        assertTrue(result.inheritsGlobal)
        assertNull(result.gameOverride)
        assertEquals(named.name, result.globalName)
        assertEquals(named.name, result.requestedName)
        assertEquals(named.name, result.nextLaunchName)
        assertTrue(result.requestable)
    }

    @Test fun perGameSystemOverridesGlobalCustomWithoutLosingRawValue() {
        val result = resolve(override = "  ")
        assertFalse(result.inheritsGlobal)
        assertEquals("  ", result.gameOverride)
        assertEquals("", result.requestedName)
        assertEquals(GpuDriverSelectionDecision.SYSTEM, result.decision)
        assertEquals("", result.nextLaunchName)
        assertTrue(result.requestable)
        assertNull(result.compatibility)
    }

    @Test fun perGameExactCustomDoesNotSelectGlobalOrFuzzyNeighbor() {
        val result = resolve(global = turnip.name, override = panvk.name)
        assertEquals(panvk.name, result.requestedName)
        assertEquals(panvk.name, result.nextLaunchName)
        assertFalse(result.inheritsGlobal)
    }

    @Test fun resetGameInheritsWhileResetGlobalSelectsSystem() {
        assertNull(GpuDriverSelectionPolicy.resetGame())
        assertEquals("", GpuDriverSelectionPolicy.resetGlobal())
        assertEquals(panvk.name, resolve(override = GpuDriverSelectionPolicy.resetGame()).requestedName)
        assertEquals("", resolve(global = GpuDriverSelectionPolicy.resetGlobal()).requestedName)
    }

    @Test fun removedCustomPreservesRequestAndFallsBackToSystemNeverAnotherPackage() {
        val result = resolve(override = "PanVK removed", packages = listOf(panvk))
        assertEquals("PanVK removed", result.requestedName)
        assertEquals("PanVK removed", result.gameOverride)
        assertEquals(GpuDriverSelectionDecision.MISSING_PACKAGE, result.decision)
        assertFalse(result.requestable)
        assertEquals("", result.nextLaunchName)
    }

    @Test fun nameMatchIsCaseSensitiveAndWhitespaceSensitive() {
        for (name in listOf("panvk 25.1", "PanVK 25.1 ", "PanVK")) {
            assertEquals(GpuDriverSelectionDecision.MISSING_PACKAGE, resolve(override = name).decision)
        }
    }

    @Test fun duplicateNamesCannotArbitrarilySelectPackageEvidence() {
        val result = resolve(packages = listOf(panvk, panvk.copy(nativeCapability = GpuDriverNativeCapability.UNSUPPORTED)))
        assertEquals(GpuDriverSelectionDecision.AMBIGUOUS_PACKAGE, result.decision)
        assertFalse(result.requestable)
        assertEquals("", result.nextLaunchName)
    }

    @Test fun panvkMaliAndTurnipAdrenoAreEligibleWithoutBlanketMaliBan() {
        assertEquals(GpuDriverCompatibilityState.COMPATIBLE, compatibility(mali, panvk).state)
        assertEquals(GpuDriverCompatibilityState.COMPATIBLE, compatibility(adreno, turnip).state)
        assertEquals(GpuDriverSelectionDecision.READY, resolve().decision)
        assertEquals(turnip.name, resolve(global = turnip.name, device = adreno).nextLaunchName)
    }

    @Test fun vortekRequiresXclipseAndDoesNotImplyProvenNativeSupport() {
        val vortek = driver("Vortek", GpuVendor.XCLIPSE, GpuDriverFamily.VORTEK, native = GpuDriverNativeCapability.UNKNOWN)
        val result = resolve(global = vortek.name, device = device(GpuVendor.XCLIPSE, "Xclipse 940"), packages = listOf(vortek))
        assertEquals(GpuDriverCompatibilityState.COMPATIBLE, result.compatibility?.state)
        assertEquals(GpuDriverSelectionDecision.UNVERIFIED_NATIVE, result.decision)
        assertTrue(result.requestable)
        assertEquals(GpuDriverRuntimeState.NO_SESSION, result.runtimeState)
    }

    @Test fun turnipOnMaliAndPanvkOnAdrenoRejectCrossVendor() {
        assertEquals(GpuDriverCompatibilityReason.CROSS_VENDOR, compatibility(mali, turnip).reason)
        assertEquals(GpuDriverCompatibilityReason.CROSS_VENDOR, compatibility(adreno, panvk).reason)
        val result = resolve(global = turnip.name)
        assertEquals(GpuDriverSelectionDecision.INCOMPATIBLE_HARDWARE, result.decision)
        assertEquals("", result.nextLaunchName)
    }

    @Test fun forgedFamilyVendorPairCannotBypassCrossVendorChecks() {
        val malformed = turnip.copy(vendor = GpuVendor.MALI)
        assertEquals(GpuDriverCompatibilityReason.FAMILY_VENDOR_MISMATCH, compatibility(mali, malformed).reason)
    }

    @Test fun powerVrIsRejectedEvenWhenPackageClaimsSameVendorAndNativeSupport() {
        val pkg = driver("PowerVR", GpuVendor.POWERVR, GpuDriverFamily.OTHER)
        val result = resolve(pkg.name, device = device(GpuVendor.POWERVR, "PowerVR BXM-8-256"), packages = listOf(pkg))
        assertEquals(GpuDriverCompatibilityReason.UNSUPPORTED_DEVICE_VENDOR, result.compatibility?.reason)
        assertFalse(result.requestable)
    }

    @Test fun unknownDeviceOrPackageVendorCannotMeanCompatible() {
        val unknown = DeviceGpuSnapshot(null, "llvmpipe", GpuInfoSource.UNKNOWN)
        assertEquals(GpuDriverCompatibilityReason.UNKNOWN_DEVICE, compatibility(unknown, panvk).reason)
        assertEquals(GpuDriverCompatibilityReason.UNKNOWN_PACKAGE_VENDOR, compatibility(mali, panvk.copy(vendor = GpuVendor.UNKNOWN)).reason)
        assertFalse(resolve(device = unknown).requestable)
        assertFalse(resolve(packages = listOf(panvk.copy(vendor = GpuVendor.UNKNOWN))).requestable)
    }

    @Test fun blankModelAndUnspecifiedPackageFamilyRemainUnverified() {
        assertEquals(GpuDriverCompatibilityState.UNVERIFIED, compatibility(device(GpuVendor.MALI, ""), panvk).state)
        assertEquals(GpuDriverCompatibilityReason.UNKNOWN_FAMILY, compatibility(mali, panvk.copy(family = GpuDriverFamily.OTHER)).reason)
    }

    @Test fun sx2RequiresPerPackageModelDeclarationAndMeasuredMali() {
        val sx2 = driver("SX2 old", GpuVendor.MALI, GpuDriverFamily.MALI_SX2, setOf("G715"))
        assertEquals(GpuDriverCompatibilityState.COMPATIBLE, compatibility(mali, sx2).state)
        for (source in listOf(GpuInfoSource.SOC_CATALOG, GpuInfoSource.UNKNOWN, GpuInfoSource.NATIVE_RENDERER)) {
            assertEquals(GpuDriverCompatibilityReason.SX2_REQUIRES_MEASURED_MALI, compatibility(mali.copy(source = source), sx2).reason)
        }
    }

    @Test fun sx2MissingVersionModelsCannotBorrowLatestOrNativeSupportedApproval() {
        val old = driver("SX2 unknown revision", GpuVendor.MALI, GpuDriverFamily.MALI_SX2)
        val latest = old.copy(name = "SX2 latest", supportedGpuModels = setOf("G715"))
        val result = resolve(old.name, packages = listOf(old, latest))
        assertEquals(GpuDriverCompatibilityReason.SX2_DECLARATIONS_MISSING, result.compatibility?.reason)
        assertEquals(GpuDriverSelectionDecision.UNVERIFIED_HARDWARE, result.decision)
        assertFalse(result.requestable)
        assertEquals("", result.nextLaunchName)
    }

    @Test fun sx2ModelMatchesWholeTokensWithRealCoreAndRevisionSuffixes() {
        val sx2 = driver("SX2", GpuVendor.MALI, GpuDriverFamily.MALI_SX2, setOf("g715"))
        for (name in listOf("Mali-G715", "Mali-G715 MC7", "Immortalis-G715 MP10", "Mali-G715 r2p0", "Mali-G715 10EE")) {
            assertEquals(name, GpuDriverCompatibilityState.COMPATIBLE, compatibility(device(GpuVendor.MALI, name), sx2).state)
        }
        for (name in listOf("Mali-G71", "Mali-G7150", "Mali-G715 Ultra", "Mali-G1 Ultra", "Mali-T715")) {
            assertNotEquals(name, GpuDriverCompatibilityState.COMPATIBLE, compatibility(device(GpuVendor.MALI, name), sx2).state)
        }
    }

    @Test fun sx2ModelDeclarationIsNotSubstringOrAnUnversionedAllowList() {
        for (declared in listOf("G71", "G7150", "G715 MC7", "anything G715", "")) {
            val sx2 = driver("SX2", GpuVendor.MALI, GpuDriverFamily.MALI_SX2, setOf(declared))
            assertNotEquals(declared, GpuDriverCompatibilityState.COMPATIBLE, compatibility(mali, sx2).state)
        }
    }

    @Test fun eligibleSx2IsRequestableButOtherPackageRevisionCannotLendItsModels() {
        val old = driver("SX2 revision1", GpuVendor.MALI, GpuDriverFamily.MALI_SX2, setOf("G57", "G78"))
        val newer = old.copy(name = "SX2 revision2", supportedGpuModels = setOf("G52", "G715"))
        assertEquals(GpuDriverSelectionDecision.READY, resolve(newer.name, packages = listOf(old, newer)).decision)
        val oldResult = resolve(old.name, packages = listOf(old, newer))
        assertEquals(GpuDriverCompatibilityReason.SX2_MODEL_NOT_DECLARED, oldResult.compatibility?.reason)
        assertFalse(oldResult.requestable)
        assertEquals("", oldResult.nextLaunchName)
    }

    @Test fun sx2AdmissionUsesTheExactPackagesDeclarationsRatherThanHardcodedOldSocList() {
        for (token in listOf("G31", "G51", "G52", "G76", "G57", "G68", "G77", "G78", "G78AE",
            "G310", "G510", "G610", "G615", "G710", "G715", "G720")) {
            val pkg = driver("SX2 version for $token", GpuVendor.MALI, GpuDriverFamily.MALI_SX2, setOf(token))
            val result = resolve(pkg.name, device = device(GpuVendor.MALI, "Mali-$token MC4"), packages = listOf(pkg))
            assertEquals(token, GpuDriverSelectionDecision.READY, result.decision)
            assertEquals(token, pkg.name, result.nextLaunchName)
        }
    }

    @Test fun explicitMeasuredG78AeSurvivesTheResolversCanonicalSuffixLoss() {
        val pkg = driver("SX2 AE", GpuVendor.MALI, GpuDriverFamily.MALI_SX2, setOf("G78AE"))
        for (renderer in listOf("ARM Mali-G78AE MC4", "ANGLE (ARM, Mali-G78AE, Vulkan 1.2)", "mali-g78ae")) {
            val measured = DeviceGpuInfoResolver.resolve(renderer, null)
            assertEquals("Mali-G78", measured.model?.displayName?.substringBefore(" MC"))
            val result = resolve(pkg.name, device = measured, packages = listOf(pkg))
            assertEquals(renderer, GpuDriverSelectionDecision.READY, result.decision)
        }
    }

    @Test fun plainMeasuredG78CannotBorrowG78AeOnlyDeclaration() {
        val pkg = driver("SX2 AE", GpuVendor.MALI, GpuDriverFamily.MALI_SX2, setOf("G78AE"))
        val measured = DeviceGpuInfoResolver.resolve("Mali-G78 MC5", null)
        assertEquals(GpuDriverCompatibilityReason.SX2_MODEL_NOT_DECLARED, compatibility(measured, pkg).reason)
    }

    @Test fun explicitMeasuredAeDoesNotBorrowPlainG78OnlyDeclaration() {
        val pkg = driver("SX2 plain", GpuVendor.MALI, GpuDriverFamily.MALI_SX2, setOf("G78"))
        val measured = DeviceGpuInfoResolver.resolve("Mali-G78AE MC4", null)
        assertEquals(GpuDriverCompatibilityReason.SX2_MODEL_NOT_DECLARED, compatibility(measured, pkg).reason)
    }

    @Test fun rawAeMatchIsBoundedAndCannotPromoteCatalogOrNativeProvenance() {
        val pkg = driver("SX2 AE", GpuVendor.MALI, GpuDriverFamily.MALI_SX2, setOf("G78AE"))
        for (renderer in listOf("Mali-G78AExtra", "Mali-G78AE2")) {
            val measured = DeviceGpuInfoResolver.resolve(renderer, null)
            assertNotEquals(renderer, GpuDriverCompatibilityState.COMPATIBLE, compatibility(measured, pkg).state)
        }
        val measured = DeviceGpuInfoResolver.resolve("Mali-G78AE MC4", null)
        for (source in listOf(GpuInfoSource.SOC_CATALOG, GpuInfoSource.NATIVE_RENDERER)) {
            assertEquals(GpuDriverCompatibilityReason.SX2_REQUIRES_MEASURED_MALI,
                compatibility(measured.copy(source = source), pkg).reason)
        }
    }

    @Test fun malformedSx2DeclarationsAreUnverifiedMetadataRatherThanUnsupportedHardware() {
        val pkg = driver("SX2 malformed", GpuVendor.MALI, GpuDriverFamily.MALI_SX2, setOf("unknown", "G715 MC7"))
        val result = resolve(pkg.name, packages = listOf(pkg))
        assertEquals(GpuDriverCompatibilityState.UNVERIFIED, result.compatibility?.state)
        assertEquals(GpuDriverSelectionDecision.UNVERIFIED_HARDWARE, result.decision)
        assertFalse(result.requestable)
    }

    @Test fun catalogMaliCanUsePanvkButUnknownIdentityProvenanceCannotProveCompatibility() {
        assertTrue(resolve(device = mali.copy(source = GpuInfoSource.SOC_CATALOG)).requestable)
        assertFalse(resolve(device = mali.copy(source = GpuInfoSource.UNKNOWN)).requestable)
    }

    @Test fun nativeSupportEvidenceDoesNotLeakBetweenPackages() {
        val unknown = panvk.copy(name = "PanVK unverified", nativeCapability = GpuDriverNativeCapability.UNKNOWN)
        val unsupported = panvk.copy(name = "PanVK unsupported", nativeCapability = GpuDriverNativeCapability.UNSUPPORTED)
        val packages = listOf(panvk, unknown, unsupported)
        assertEquals(GpuDriverSelectionDecision.UNVERIFIED_NATIVE, resolve(unknown.name, packages = packages).decision)
        assertEquals(GpuDriverSelectionDecision.NATIVE_UNSUPPORTED, resolve(unsupported.name, packages = packages).decision)
    }

    @Test fun nativeUnknownKeepsCompatibleRequestableButDoesNotClaimLoaded() {
        val pkg = panvk.copy(nativeCapability = GpuDriverNativeCapability.UNKNOWN)
        val result = resolve(packages = listOf(pkg))
        assertEquals(GpuDriverCompatibilityState.COMPATIBLE, result.compatibility?.state)
        assertEquals(GpuDriverNativeCapability.UNKNOWN, result.nativeCapability)
        assertEquals(GpuDriverSelectionDecision.UNVERIFIED_NATIVE, result.decision)
        assertTrue(result.requestable)
        assertEquals(pkg.name, result.nextLaunchName)
        assertEquals(GpuDriverRuntimeState.NO_SESSION, result.runtimeState)
        assertNull(result.activeCustomName)
    }

    @Test fun nativeUnsupportedIsSeparateFromCompatibleHardwareAndDeniesNextLaunch() {
        val result = resolve(packages = listOf(panvk.copy(nativeCapability = GpuDriverNativeCapability.UNSUPPORTED)))
        assertEquals(GpuDriverCompatibilityState.COMPATIBLE, result.compatibility?.state)
        assertEquals(GpuDriverSelectionDecision.NATIVE_UNSUPPORTED, result.decision)
        assertFalse(result.requestable)
        assertEquals("", result.nextLaunchName)
        assertEquals(panvk.name, result.requestedName)
    }

    @Test fun invalidMissingAndUnverifiedFilesCannotLoadEvenOnCompatibleHardware() {
        val expected = mapOf(GpuDriverFileState.INVALID to GpuDriverSelectionDecision.INVALID_FILE,
            GpuDriverFileState.MISSING to GpuDriverSelectionDecision.MISSING_FILE,
            GpuDriverFileState.UNKNOWN to GpuDriverSelectionDecision.UNVERIFIED_FILE)
        for ((state, decision) in expected) {
            val result = resolve(packages = listOf(panvk.copy(fileState = state)))
            assertEquals(decision, result.decision)
            assertEquals(GpuDriverCompatibilityState.COMPATIBLE, result.compatibility?.state)
            assertFalse(result.requestable)
            assertEquals("", result.nextLaunchName)
        }
    }

    @Test fun savedCustomAndUnknownRuntimeNeverMeanLoadedOrSystemLoaded() {
        val runtime = GpuDriverRuntimeEvidence(GpuDriverBackend.VULKAN)
        val result = resolve(runtime = runtime)
        assertEquals(GpuDriverRuntimeState.UNVERIFIED, result.runtimeState)
        assertEquals(GpuDriverLoadedKind.UNKNOWN, result.runtime?.loadedKind)
        assertNull(result.activeCustomName)
        assertEquals(GpuDriverRuntimeState.UNVERIFIED, resolve(global = "", runtime = runtime).runtimeState)
    }

    @Test fun actualNativeCustomEvidenceCanProveLoadedWithoutPromotingHardwareSnapshot() {
        val runtime = GpuDriverRuntimeEvidence(GpuDriverBackend.VULKAN, GpuDriverLoadedKind.CUSTOM, panvk.name)
        val result = resolve(runtime = runtime)
        assertEquals(GpuDriverRuntimeState.LOADED, result.runtimeState)
        assertEquals(panvk.name, result.activeCustomName)
        assertEquals(runtime, result.runtime)
    }

    @Test fun customKindWithoutActualNameIsStillUnverified() {
        val result = resolve(runtime = GpuDriverRuntimeEvidence(GpuDriverBackend.VULKAN, GpuDriverLoadedKind.CUSTOM))
        assertEquals(GpuDriverRuntimeState.UNVERIFIED, result.runtimeState)
        assertNull(result.activeCustomName)
    }

    @Test fun loadedSystemWithSavedCustomDoesNotGuessFallbackOrRestart() {
        val runtime = GpuDriverRuntimeEvidence(GpuDriverBackend.VULKAN, GpuDriverLoadedKind.SYSTEM)
        val result = resolve(runtime = runtime)
        assertEquals(GpuDriverRuntimeState.UNVERIFIED, result.runtimeState)
        assertEquals(GpuDriverLoadedKind.SYSTEM, result.runtime?.loadedKind)
    }

    @Test fun launchRequestDistinguishesRealFallbackFromNewPendingSelection() {
        val system = GpuDriverRuntimeEvidence(GpuDriverBackend.VULKAN, GpuDriverLoadedKind.SYSTEM)
        assertEquals(GpuDriverRuntimeState.FALLBACK, resolve(runtime = system.copy(requestedAtLaunch = panvk.name)).runtimeState)
        assertEquals(GpuDriverRuntimeState.RESTART_REQUIRED, resolve(runtime = system.copy(requestedAtLaunch = "")).runtimeState)
    }

    @Test fun explicitFallbackEvidenceMustMatchCurrentRequest() {
        val runtime = GpuDriverRuntimeEvidence(GpuDriverBackend.VULKAN, GpuDriverLoadedKind.SYSTEM, fallbackForRequest = panvk.name)
        assertEquals(GpuDriverRuntimeState.FALLBACK, resolve(runtime = runtime).runtimeState)
        assertEquals(GpuDriverRuntimeState.UNVERIFIED, resolve(runtime = runtime.copy(fallbackForRequest = "removed package")).runtimeState)
    }

    @Test fun changedRequestCanRequireRestartWhileActualLoadedEvidenceRemainsAvailable() {
        val runtime = GpuDriverRuntimeEvidence(GpuDriverBackend.VULKAN, GpuDriverLoadedKind.CUSTOM,
            "old package", requestedAtLaunch = "old package")
        val result = resolve(runtime = runtime)
        assertEquals(GpuDriverRuntimeState.RESTART_REQUIRED, result.runtimeState)
        assertEquals("old package", result.activeCustomName)
        assertEquals("old package", result.runtime?.loadedCustomName)
        assertEquals(GpuDriverRuntimeState.UNVERIFIED, resolve(runtime = runtime.copy(requestedAtLaunch = null)).runtimeState)
    }

    @Test fun openGlKeepsVulkanRequestSavedButNeverCallsItActiveOrFallback() {
        val runtime = GpuDriverRuntimeEvidence(GpuDriverBackend.OPENGL, GpuDriverLoadedKind.SYSTEM,
            requestedAtLaunch = panvk.name)
        val result = resolve(runtime = runtime)
        assertEquals(GpuDriverRuntimeState.INACTIVE_BACKEND, result.runtimeState)
        assertEquals(panvk.name, result.requestedName)
        assertEquals(panvk.name, result.nextLaunchName)
        assertNull(result.activeCustomName)
        assertNull(resolve(runtime = runtime.copy(loadedKind = GpuDriverLoadedKind.CUSTOM,
            loadedCustomName = panvk.name)).activeCustomName)
    }

    @Test fun unknownBackendCannotProveCustomDriverActive() {
        val runtime = GpuDriverRuntimeEvidence(GpuDriverBackend.UNKNOWN, GpuDriverLoadedKind.CUSTOM, panvk.name)
        val result = resolve(runtime = runtime)
        assertEquals(GpuDriverRuntimeState.UNVERIFIED, result.runtimeState)
        assertNull(result.activeCustomName)
    }

    @Test fun actualSystemLoadedRequiresEvidenceAndBlankLaunchRemainsKnownSystem() {
        val runtime = GpuDriverRuntimeEvidence(GpuDriverBackend.VULKAN, GpuDriverLoadedKind.SYSTEM, requestedAtLaunch = "  ")
        assertEquals(GpuDriverRuntimeState.LOADED, resolve(global = "", runtime = runtime).runtimeState)
        assertEquals(GpuDriverRuntimeState.RESTART_REQUIRED, resolve(runtime = runtime).runtimeState)
    }
}
