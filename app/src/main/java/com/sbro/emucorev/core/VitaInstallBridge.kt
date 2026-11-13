package com.sbro.emucorev.core

import android.content.Context

data class NativeInstallProgress(
    val stage: String,
    val progress: Float,
    val current: Float,
    val total: Float,
    val detail: String?
)

data class VitaPkgInfo(
    val kind: Int,
    val contentId: String
) {
    val isDlc: Boolean get() = kind == KIND_DLC

    /** Title id encoded in the package content id, when it follows the standard layout. */
    val titleId: String?
        get() = contentId.takeIf { it.length >= 16 }?.substring(7, 16)?.takeIf { it.isNotBlank() }

    companion object {
        const val KIND_UNKNOWN = 0
        const val KIND_APP = 1
        const val KIND_DLC = 2
        const val KIND_THEME = 3
    }
}

internal data class VitaEmbeddedInstallResult(
    val kind: InstallerPackageKind,
    val success: Boolean,
    val pkgInfo: VitaPkgInfo? = null,
    val installedContentCount: Int = 0
)

object VitaInstallBridge {
    fun interface Listener {
        fun onProgress(progress: NativeInstallProgress)
    }

    @Volatile
    private var listener: Listener? = null

    fun setListener(listener: Listener?) {
        this.listener = listener
    }

    /** Inspects ZIP wrappers using signatures and valid nested Vita SFO metadata. */
    internal fun scanEmbeddedPackages(
        context: Context,
        archivePath: String,
        isCancelled: () -> Boolean = { false }
    ): InstallerPackageSelection = VitaEmbeddedPackageSupport.scanZip(
        archivePath = archivePath,
        stagingRoot = EmulatorStorage.cacheRoot(context),
        isCancelled = isCancelled
    )

    /** Stages only the selected member into a private, atomically promoted cache session. */
    internal fun prepareEmbeddedPackage(
        context: Context,
        archivePath: String,
        candidate: InstallerPackageCandidate,
        isCancelled: () -> Boolean = { false },
        onProgress: (EmbeddedPackageProgress) -> Unit = {}
    ): PreparedEmbeddedPackage = VitaEmbeddedPackageSupport.extractSelected(
        archivePath = archivePath,
        candidate = candidate,
        stagingRoot = EmulatorStorage.cacheRoot(context),
        isCancelled = isCancelled,
        onProgress = onProgress
    )

    /** Opens a signature-identified ZIP/RAR/7z container and recursively finds staged Vita packages. */
    internal fun scanArchivePackages(
        context: Context,
        archivePath: String,
        isCancelled: () -> Boolean = { false },
        onProgress: (EmbeddedPackageProgress) -> Unit = {}
    ): VitaArchivePackagePlan? {
        CoreMaintenanceRepository(context).ensureCoreUpdated()
        NativeLibraryLoader.ensureLoaded(context)
        return VitaArchiveContainerSupport.scan(
            archivePath = archivePath,
            stagingRoot = EmulatorStorage.cacheRoot(context),
            isCancelled = isCancelled,
            onProgress = onProgress
        )
    }

    /** Routes selected content through the existing native VPK or licensed PKG installer. */
    internal fun installPreparedEmbeddedPackage(
        context: Context,
        prepared: PreparedEmbeddedPackage,
        systemLanguage: Int,
        zrif: String = ""
    ): VitaEmbeddedInstallResult = when (prepared.candidate.kind) {
        InstallerPackageKind.VPK, InstallerPackageKind.CONTENT_DIRECTORY -> {
            val count = installContent(context, prepared.file.absolutePath, systemLanguage)
            VitaEmbeddedInstallResult(prepared.candidate.kind, count > 0, installedContentCount = count)
        }
        InstallerPackageKind.PKG -> {
            val pkgInfo = inspectPkg(context, prepared.file.absolutePath)
            if (pkgInfo.kind == VitaPkgInfo.KIND_UNKNOWN || pkgInfo.contentId.isBlank()) {
                VitaEmbeddedInstallResult(InstallerPackageKind.PKG, false, pkgInfo)
            } else {
                VitaEmbeddedInstallResult(
                    InstallerPackageKind.PKG,
                    installPkg(context, prepared.file.absolutePath, zrif, systemLanguage),
                    pkgInfo
                )
            }
        }
    }

    fun installFirmware(
        context: Context,
        firmwarePath: String,
        systemLanguage: Int
    ): String? {
        CoreMaintenanceRepository(context).ensureCoreUpdated()
        NativeLibraryLoader.ensureLoaded(context)
        return nativeInstallFirmware(
            EmulatorStorage.vitaRoot(context).absolutePath,
            firmwarePath,
            systemLanguage
        )
    }

    fun installContent(
        context: Context,
        contentPath: String,
        systemLanguage: Int
    ): Int {
        CoreMaintenanceRepository(context).ensureCoreUpdated()
        NativeLibraryLoader.ensureLoaded(context)
        val installedCount = nativeInstallContent(
            EmulatorStorage.vitaRoot(context).absolutePath,
            EmulatorStorage.cacheRoot(context).absolutePath,
            contentPath,
            systemLanguage
        )
        if (installedCount > 0) {
            NativeLib.refreshAppsList()
        }
        return installedCount
    }

    fun installLicense(
        context: Context,
        licensePath: String,
        systemLanguage: Int
    ): Boolean {
        CoreMaintenanceRepository(context).ensureCoreUpdated()
        NativeLibraryLoader.ensureLoaded(context)
        return nativeInstallLicense(
            EmulatorStorage.vitaRoot(context).absolutePath,
            EmulatorStorage.cacheRoot(context).absolutePath,
            licensePath,
            systemLanguage
        )
    }

    fun installPkg(
        context: Context,
        pkgPath: String,
        zrif: String,
        systemLanguage: Int
    ): Boolean {
        CoreMaintenanceRepository(context).ensureCoreUpdated()
        NativeLibraryLoader.ensureLoaded(context)
        val success = nativeInstallPkg(
            EmulatorStorage.vitaRoot(context).absolutePath,
            EmulatorStorage.cacheRoot(context).absolutePath,
            pkgPath,
            zrif,
            systemLanguage
        )
        if (success) {
            NativeLib.refreshAppsList()
        }
        return success
    }

    /**
     * Reads the unencrypted PKG header. Used to keep DLC installs separate from
     * game/update installs so the UI can show a precise result and error.
     */
    fun inspectPkg(context: Context, pkgPath: String): VitaPkgInfo {
        CoreMaintenanceRepository(context).ensureCoreUpdated()
        NativeLibraryLoader.ensureLoaded(context)
        val raw = runCatching { nativeInspectPkg(pkgPath) }.getOrNull().orEmpty()
        val separator = raw.indexOf('|')
        if (separator <= 0) return VitaPkgInfo(VitaPkgInfo.KIND_UNKNOWN, "")
        val kind = raw.substring(0, separator).toIntOrNull() ?: VitaPkgInfo.KIND_UNKNOWN
        return VitaPkgInfo(kind, raw.substring(separator + 1))
    }

    @JvmStatic
    fun onNativeProgress(
        stage: String,
        progress: Float,
        current: Float,
        total: Float,
        detail: String?
    ) {
        listener?.onProgress(
            NativeInstallProgress(
                stage = stage,
                progress = progress,
                current = current,
                total = total,
                detail = detail
            )
        )
    }

    private external fun nativeInstallFirmware(
        vitaRootPath: String,
        firmwarePath: String,
        systemLanguage: Int
    ): String?

    private external fun nativeInstallContent(
        vitaRootPath: String,
        cacheRootPath: String,
        contentPath: String,
        systemLanguage: Int
    ): Int

    private external fun nativeInstallLicense(
        vitaRootPath: String,
        cacheRootPath: String,
        licensePath: String,
        systemLanguage: Int
    ): Boolean

    private external fun nativeInstallPkg(
        vitaRootPath: String,
        cacheRootPath: String,
        pkgPath: String,
        zrif: String,
        systemLanguage: Int
    ): Boolean

    private external fun nativeInspectPkg(pkgPath: String): String
}
