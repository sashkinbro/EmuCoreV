package com.sbro.emucorev.core

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.widget.Toast
import com.sbro.emucorev.R
import com.sbro.emucorev.data.AppPreferences
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

internal enum class CoreUpdateResetAction { NONE, STORE_SILENTLY, RESET_AUTOMATICALLY }

/**
 * Decides how to react to the core fingerprint of the installed build.
 *
 * Fresh installs only record the fingerprint, upgrades reset automatically once per changed core,
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
            CoreUpdateResetAction.RESET_AUTOMATICALLY
        } else {
            CoreUpdateResetAction.STORE_SILENTLY
        }
    }
    return if (storedFingerprint == currentFingerprint) {
        CoreUpdateResetAction.NONE
    } else {
        CoreUpdateResetAction.RESET_AUTOMATICALLY
    }
}

class CoreMaintenanceRepository(context: Context) {

    private val appContext = context.applicationContext

    /** Shared by navigation, game launch and installers; synchronous work runs on IO. */
    fun ensureCoreUpdated(): Boolean = synchronized(updateLock) {
        val preferences = AppPreferences(appContext)
        val fingerprint = CoreBinaryFingerprint.current(appContext)
        when (decideCoreUpdateResetAction(
            preferences.lastCoreBinaryFingerprint, fingerprint, preferences.onboardingCompleted
        )) {
            CoreUpdateResetAction.STORE_SILENTLY -> {
                preferences.lastCoreBinaryFingerprint = fingerprint
                false
            }
            CoreUpdateResetAction.RESET_AUTOMATICALLY -> {
                resetGeneratedCoreState()
                VitaCoreConfigRepository(appContext).ensureDefaultsPersisted()
                // Persist inside the same lock even if the initiating screen was destroyed.
                preferences.lastCoreBinaryFingerprint = fingerprint
                Handler(Looper.getMainLooper()).post {
                    Toast.makeText(appContext, R.string.core_update_reset_done, Toast.LENGTH_SHORT).show()
                }
                true
            }
            CoreUpdateResetAction.NONE -> false
        }
    }

    fun resetGeneratedCoreState(): Int {
        val root = EmulatorStorage.runtimeRoot(appContext)
        val deleted = clearRegenerableCoreState(root)
        check(REGENERABLE_CORE_ENTRIES.none { File(root, it).exists() }) {
            "Unable to clear all generated core state"
        }
        return deleted
    }

    private companion object {
        val updateLock = Any()
    }
}
