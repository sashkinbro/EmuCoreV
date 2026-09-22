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

object VitaInstallBridge {
    fun interface Listener {
        fun onProgress(progress: NativeInstallProgress)
    }

    @Volatile
    private var listener: Listener? = null

    fun setListener(listener: Listener?) {
        this.listener = listener
    }

    fun installFirmware(
        context: Context,
        firmwarePath: String,
        systemLanguage: Int
    ): String? {
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
