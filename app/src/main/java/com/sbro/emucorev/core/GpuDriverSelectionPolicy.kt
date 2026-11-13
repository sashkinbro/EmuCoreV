package com.sbro.emucorev.core

import java.util.Locale

enum class GpuDriverFamily { TURNIP, PANVK, MALI_SX2, VORTEK, OTHER }
enum class GpuDriverFileState { VALID, INVALID, MISSING, UNKNOWN }
enum class GpuDriverNativeCapability { SUPPORTED, UNSUPPORTED, UNKNOWN }
enum class GpuDriverCompatibilityState { COMPATIBLE, INCOMPATIBLE, UNVERIFIED }
enum class GpuDriverCompatibilityReason {
    MATCH, UNKNOWN_DEVICE, UNSUPPORTED_DEVICE_VENDOR, UNKNOWN_PACKAGE_VENDOR,
    CROSS_VENDOR, FAMILY_VENDOR_MISMATCH, UNKNOWN_FAMILY,
    SX2_REQUIRES_MEASURED_MALI, SX2_DECLARATIONS_MISSING, SX2_DECLARATIONS_UNVERIFIED,
    SX2_MODEL_UNVERIFIED, SX2_MODEL_NOT_DECLARED,
    PACKAGE_MODEL_NOT_DECLARED, PACKAGE_REQUIREMENTS_UNVERIFIED
}

/**
 * Evidence for this exact package/version, not a library filename or the latest catalog entry.
 * File validity must not be inferred from a legacy flag that also folds in GPU compatibility.
 * Native capability is package-specific; UNKNOWN permits an eligible attempt, not a load claim.
 */
data class GpuDriverPackageEvidence(
    val name: String,
    val vendor: GpuVendor,
    val family: GpuDriverFamily,
    val supportedGpuModels: Set<String> = emptySet(),
    val fileState: GpuDriverFileState = GpuDriverFileState.UNKNOWN,
    val nativeCapability: GpuDriverNativeCapability = GpuDriverNativeCapability.UNKNOWN
)

data class GpuDriverSelectionCompatibility(
    val state: GpuDriverCompatibilityState,
    val reason: GpuDriverCompatibilityReason
)

enum class GpuDriverSelectionDecision {
    SYSTEM, READY, UNVERIFIED_NATIVE, MISSING_PACKAGE, AMBIGUOUS_PACKAGE,
    INVALID_FILE, MISSING_FILE, UNVERIFIED_FILE, INCOMPATIBLE_HARDWARE,
    UNVERIFIED_HARDWARE, NATIVE_UNSUPPORTED
}

enum class GpuDriverBackend { VULKAN, OPENGL, UNKNOWN }
enum class GpuDriverLoadedKind { UNKNOWN, SYSTEM, CUSTOM }
enum class GpuDriverRuntimeState {
    NO_SESSION, UNVERIFIED, INACTIVE_BACKEND, LOADED, FALLBACK, RESTART_REQUIRED
}

/**
 * Native/session evidence, never values inferred from saved selection. null launch request means
 * unknown; blank means System. [fallbackForRequest] identifies the exact failed custom request.
 */
data class GpuDriverRuntimeEvidence(
    val backend: GpuDriverBackend,
    val loadedKind: GpuDriverLoadedKind = GpuDriverLoadedKind.UNKNOWN,
    val loadedCustomName: String? = null,
    val requestedAtLaunch: String? = null,
    val fallbackForRequest: String? = null
)

data class GpuDriverSelectionResult(
    val globalName: String,
    val gameOverride: String?,
    val requestedName: String,
    val inheritsGlobal: Boolean,
    val compatibility: GpuDriverSelectionCompatibility?,
    val nativeCapability: GpuDriverNativeCapability?,
    val decision: GpuDriverSelectionDecision,
    val requestable: Boolean,
    /** Recommendation only. Empty selects System, never a neighboring installed package. */
    val nextLaunchName: String,
    val runtime: GpuDriverRuntimeEvidence?,
    val runtimeState: GpuDriverRuntimeState,
    val activeCustomName: String?
)

/** Pure selection policy; does not probe hardware, load a driver, or persist settings. */
internal object GpuDriverSelectionPolicy {
    private val SX2_MODEL = Regex(
        """^(?:mali|immortalis)[\s-]*g\s*([1-9]\d{1,2}(?:ae)?)(?:[\s-]*(?:(?:mc|mp)\s*[1-9]\d?|\d{1,2}ee|r\d+(?:p\d+)?))*$""",
        RegexOption.IGNORE_CASE
    )
    private val SX2_DECLARATION = Regex("""^G[1-9]\d{1,2}(?:AE)?$""")
    private val SX2_RAW_AE = Regex(
        """(?<![a-z0-9])(?:mali|immortalis)[\s-]*g\s*([1-9]\d{1,2}ae)(?![a-z0-9])""",
        RegexOption.IGNORE_CASE
    )

    fun compatibility(device: DeviceGpuSnapshot, driver: GpuDriverPackageEvidence): GpuDriverSelectionCompatibility {
        val model = device.model
        if (model == null || model.vendor == GpuVendor.UNKNOWN || model.displayName.isBlank()) {
            return unverified(GpuDriverCompatibilityReason.UNKNOWN_DEVICE)
        }
        if (model.vendor == GpuVendor.POWERVR) {
            return incompatible(GpuDriverCompatibilityReason.UNSUPPORTED_DEVICE_VENDOR)
        }
        if (driver.vendor == GpuVendor.UNKNOWN) {
            return unverified(GpuDriverCompatibilityReason.UNKNOWN_PACKAGE_VENDOR)
        }
        if (driver.vendor != model.vendor) return incompatible(GpuDriverCompatibilityReason.CROSS_VENDOR)
        val familyVendor = when (driver.family) {
            GpuDriverFamily.TURNIP -> GpuVendor.ADRENO
            GpuDriverFamily.PANVK, GpuDriverFamily.MALI_SX2 -> GpuVendor.MALI
            GpuDriverFamily.VORTEK -> GpuVendor.XCLIPSE
            GpuDriverFamily.OTHER -> return unverified(GpuDriverCompatibilityReason.UNKNOWN_FAMILY)
        }
        if (driver.vendor != familyVendor) return incompatible(GpuDriverCompatibilityReason.FAMILY_VENDOR_MISMATCH)
        if (driver.family == GpuDriverFamily.MALI_SX2) {
            // A SoC guess or an active native renderer is not measured physical Mali evidence.
            if (device.source != GpuInfoSource.GL_RENDERER) {
                return unverified(GpuDriverCompatibilityReason.SX2_REQUIRES_MEASURED_MALI)
            }
            if (driver.supportedGpuModels.isEmpty()) {
                return unverified(GpuDriverCompatibilityReason.SX2_DECLARATIONS_MISSING)
            }
            val match = SX2_MODEL.matchEntire(model.displayName.trim())
                ?: return unverified(GpuDriverCompatibilityReason.SX2_MODEL_UNVERIFIED)
            val canonicalToken = "G" + match.groupValues[1].uppercase(Locale.US)
            // The shared parser currently drops AE. Preserve an explicit matching raw GL variant;
            // plain G78 must not inherit AE-only support, nor AE inherit plain-G78-only support.
            val explicitAe = device.renderer?.let { raw ->
                SX2_RAW_AE.findAll(raw).map { "G" + it.groupValues[1].uppercase(Locale.US) }
                    .firstOrNull { it == canonicalToken || it == canonicalToken + "AE" }
            }
            val token = explicitAe ?: canonicalToken
            val declared = driver.supportedGpuModels.map { it.trim().uppercase(Locale.US) }
                .filter { SX2_DECLARATION.matches(it) }
            if (declared.isEmpty()) return unverified(GpuDriverCompatibilityReason.SX2_DECLARATIONS_UNVERIFIED)
            if (token !in declared) return incompatible(GpuDriverCompatibilityReason.SX2_MODEL_NOT_DECLARED)
        } else if (device.source == GpuInfoSource.UNKNOWN) {
            return unverified(GpuDriverCompatibilityReason.UNKNOWN_DEVICE)
        }
        return GpuDriverSelectionCompatibility(GpuDriverCompatibilityState.COMPATIBLE, GpuDriverCompatibilityReason.MATCH)
    }

    fun resolve(
        globalName: String,
        gameOverride: String?,
        device: DeviceGpuSnapshot,
        packages: List<GpuDriverPackageEvidence>,
        runtime: GpuDriverRuntimeEvidence? = null
    ): GpuDriverSelectionResult {
        // Only blank is canonicalized. Nonblank names are exact installed-directory identities.
        val requested = canonicalName(gameOverride ?: globalName)
        val matches = if (requested.isEmpty()) emptyList() else packages.filter { it.name == requested }
        val pkg = matches.singleOrNull()
        val compatibility = pkg?.let { compatibility(device, it) }
        val decision = when {
            requested.isEmpty() -> GpuDriverSelectionDecision.SYSTEM
            matches.isEmpty() -> GpuDriverSelectionDecision.MISSING_PACKAGE
            matches.size > 1 -> GpuDriverSelectionDecision.AMBIGUOUS_PACKAGE
            pkg!!.fileState == GpuDriverFileState.INVALID -> GpuDriverSelectionDecision.INVALID_FILE
            pkg.fileState == GpuDriverFileState.MISSING -> GpuDriverSelectionDecision.MISSING_FILE
            pkg.fileState == GpuDriverFileState.UNKNOWN -> GpuDriverSelectionDecision.UNVERIFIED_FILE
            compatibility!!.state == GpuDriverCompatibilityState.INCOMPATIBLE -> GpuDriverSelectionDecision.INCOMPATIBLE_HARDWARE
            compatibility.state == GpuDriverCompatibilityState.UNVERIFIED -> GpuDriverSelectionDecision.UNVERIFIED_HARDWARE
            pkg.nativeCapability == GpuDriverNativeCapability.UNSUPPORTED -> GpuDriverSelectionDecision.NATIVE_UNSUPPORTED
            pkg.nativeCapability == GpuDriverNativeCapability.UNKNOWN -> GpuDriverSelectionDecision.UNVERIFIED_NATIVE
            else -> GpuDriverSelectionDecision.READY
        }
        val requestable = decision == GpuDriverSelectionDecision.SYSTEM ||
            decision == GpuDriverSelectionDecision.READY || decision == GpuDriverSelectionDecision.UNVERIFIED_NATIVE
        return GpuDriverSelectionResult(
            globalName = globalName,
            gameOverride = gameOverride,
            requestedName = requested,
            inheritsGlobal = gameOverride == null,
            compatibility = compatibility,
            nativeCapability = pkg?.nativeCapability,
            decision = decision,
            requestable = requestable,
            nextLaunchName = if (requestable) requested else "",
            runtime = runtime,
            runtimeState = runtimeState(requested, runtime),
            activeCustomName = runtime?.takeIf {
                it.backend == GpuDriverBackend.VULKAN && it.loadedKind == GpuDriverLoadedKind.CUSTOM
            }?.loadedCustomName?.takeUnless { it.isBlank() }
        )
    }

    private fun runtimeState(requested: String, runtime: GpuDriverRuntimeEvidence?): GpuDriverRuntimeState {
        if (runtime == null) return GpuDriverRuntimeState.NO_SESSION
        if (runtime.backend == GpuDriverBackend.OPENGL) return GpuDriverRuntimeState.INACTIVE_BACKEND
        if (runtime.backend != GpuDriverBackend.VULKAN) return GpuDriverRuntimeState.UNVERIFIED
        val launch = runtime.requestedAtLaunch?.let(::canonicalName)
        if (launch != null && launch != requested) return GpuDriverRuntimeState.RESTART_REQUIRED
        return when (runtime.loadedKind) {
            GpuDriverLoadedKind.UNKNOWN -> GpuDriverRuntimeState.UNVERIFIED
            GpuDriverLoadedKind.SYSTEM -> when {
                requested.isEmpty() -> GpuDriverRuntimeState.LOADED
                launch == requested || runtime.fallbackForRequest == requested -> GpuDriverRuntimeState.FALLBACK
                else -> GpuDriverRuntimeState.UNVERIFIED
            }
            GpuDriverLoadedKind.CUSTOM -> if (requested.isNotEmpty() && runtime.loadedCustomName == requested) {
                GpuDriverRuntimeState.LOADED
            } else GpuDriverRuntimeState.UNVERIFIED
        }
    }

    private fun canonicalName(name: String): String = if (name.isBlank()) "" else name

    private fun unverified(reason: GpuDriverCompatibilityReason) =
        GpuDriverSelectionCompatibility(GpuDriverCompatibilityState.UNVERIFIED, reason)

    private fun incompatible(reason: GpuDriverCompatibilityReason) =
        GpuDriverSelectionCompatibility(GpuDriverCompatibilityState.INCOMPATIBLE, reason)

    fun resetGlobal(): String = ""
    fun resetGame(): String? = null
}
