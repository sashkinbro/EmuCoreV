package com.sbro.emucorev.core

import android.content.Context
import java.io.File

/** Core-owned runtime entries that are rebuilt on demand and never contain user data. */
internal val REGENERABLE_CORE_ENTRIES =
    listOf("config.yml", "config", "cache", "shaderlog", "texturelog")

/** Deletes regenerable core entries; user files such as patches, play time and saves stay. */
internal fun clearRegenerableCoreState(runtimeRoot: File): Int {
    var deletedFiles = 0
    for (name in REGENERABLE_CORE_ENTRIES) {
        val entry = File(runtimeRoot, name)
        when {
            entry.isFile -> {
                if (entry.delete()) deletedFiles++
            }
            entry.isDirectory -> {
                entry.walkBottomUp().forEach { child ->
                    if (child == entry) return@forEach
                    if (child.isFile) {
                        if (child.delete()) deletedFiles++
                    } else if (child.isDirectory) {
                        child.delete()
                    }
                }
                entry.delete()
            }
        }
    }
    return deletedFiles
}

internal enum class CoreUpdateResetAction { NONE, STORE_SILENTLY, PROMPT }

/**
 * Decides how to react to the core fingerprint of the installed build.
 *
 * Fresh installs only record the fingerprint, upgrades prompt once per changed core,
 * and an unchanged core does nothing.
 */
internal fun decideCoreUpdateResetAction(
    storedFingerprint: String?,
    currentFingerprint: String?,
    hadExistingInstall: Boolean
): CoreUpdateResetAction {
    if (currentFingerprint.isNullOrBlank()) return CoreUpdateResetAction.NONE
    if (storedFingerprint.isNullOrBlank()) {
        return if (hadExistingInstall) {
            CoreUpdateResetAction.PROMPT
        } else {
            CoreUpdateResetAction.STORE_SILENTLY
        }
    }
    return if (storedFingerprint == currentFingerprint) {
        CoreUpdateResetAction.NONE
    } else {
        CoreUpdateResetAction.PROMPT
    }
}

class CoreMaintenanceRepository(context: Context) {

    private val appContext = context.applicationContext

    fun resetGeneratedCoreState(): Int =
        clearRegenerableCoreState(EmulatorStorage.runtimeRoot(appContext))
}
