package com.sbro.emucorev.core

import android.content.Context
import java.io.File

object EmulatorStorage {
    fun vitaRoot(context: Context): File {
        val base = context.getExternalFilesDir(null) ?: context.filesDir
        return File(base, "vita").apply { mkdirs() }
    }

    fun cacheRoot(context: Context): File {
        val base = context.externalCacheDir ?: context.cacheDir
        return File(base, "vita_cache").apply { mkdirs() }
    }

    fun prepareRuntime(context: Context) {
        val vitaRoot = vitaRoot(context)
        val cacheRoot = cacheRoot(context)
        listOf(
            vitaRoot,
            cacheRoot,
            File(vitaRoot, "ux0"),
            File(vitaRoot, "ux0/app"),
            File(vitaRoot, "ux0/data"),
            File(vitaRoot, "ux0/user"),
            File(vitaRoot, "vs0"),
            File(vitaRoot, "shaderlog"),
            File(cacheRoot, "shaders"),
            File(cacheRoot, "logs")
        ).forEach { directory ->
            if (!directory.exists()) {
                directory.mkdirs()
            }
        }
    }

    fun ux0AppRoot(context: Context): File = File(vitaRoot(context), "ux0/app").apply { mkdirs() }

    fun hasInstalledFirmware(context: Context): Boolean {
        val firmwareRoot = File(vitaRoot(context), "vs0")
        if (!firmwareRoot.exists() || !firmwareRoot.isDirectory) return false
        return firmwareRoot.walkTopDown().any { it.isFile }
    }

    fun hasInstalledFirmwareUpdate(context: Context): Boolean {
        val updateRoot = File(vitaRoot(context), "sa0")
        if (!updateRoot.exists() || !updateRoot.isDirectory) return false
        return updateRoot.walkTopDown().any { it.isFile }
    }

    fun iconPath(context: Context, titleId: String): File =
        File(ux0AppRoot(context), "$titleId/sce_sys/icon0.png")

    fun paramSfoPath(context: Context, titleId: String): File =
        File(ux0AppRoot(context), "$titleId/sce_sys/param.sfo")
}
