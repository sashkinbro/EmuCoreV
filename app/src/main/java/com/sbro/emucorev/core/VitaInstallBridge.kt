package com.sbro.emucorev.core

import android.content.Context

data class NativeInstallProgress(
    val stage: String,
    val progress: Float,
    val current: Float,
    val total: Float,
    val detail: String?
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
        return nativeInstallContent(
            EmulatorStorage.vitaRoot(context).absolutePath,
            EmulatorStorage.cacheRoot(context).absolutePath,
            contentPath,
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
        return nativeInstallPkg(
            EmulatorStorage.vitaRoot(context).absolutePath,
            EmulatorStorage.cacheRoot(context).absolutePath,
            pkgPath,
            zrif,
            systemLanguage
        )
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

    private external fun nativeInstallPkg(
        vitaRootPath: String,
        cacheRootPath: String,
        pkgPath: String,
        zrif: String,
        systemLanguage: Int
    ): Boolean
}
