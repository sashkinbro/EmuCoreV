package com.sbro.emucorev

import android.app.Application
import com.sbro.emucorev.core.AndroidDiagnostics
import com.sbro.emucorev.core.BackupSessionGate
import com.sbro.emucorev.core.NativeLib
import com.sbro.emucorev.core.AppIconManager
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.core.NativeLibraryLoader
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.ProfilePlayTimeSyncer
import com.sbro.emucorev.data.drive.DriveBackupArchive
import com.sbro.emucorev.discord.DiscordIntegration
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.runBlocking

class EmuCoreVApp : Application() {
    override fun onCreate() {
        super.onCreate()
        // ProcessPhoenix restarts the app through a helper process where Firebase providers
        // are not initialized, and the Discord SDK runs in its own isolated process; neither
        // helper may build the emulator-side graph.
        val processName = Application.getProcessName()
        if (processName.endsWith(PHOENIX_PROCESS_SUFFIX) || processName.endsWith(DISCORD_PROCESS_SUFFIX)) return
        AndroidDiagnostics.initialize(this)
        AppIconManager.applyProIcon(this, AppPreferences(this).proUnlocked)
        ProfilePlayTimeSyncer.syncPendingAsync(this)
        recoverPendingDriveRestore()
        DiscordIntegration.initialize(this)
        runCatching {
            EmulatorStorage.prepareRuntime(this)
            VitaCoreConfigRepository(this).ensureDefaultsPersisted()
        }
        if (AppPreferences(this).onboardingCompleted) {
            // Native init (Vulkan enumeration, app-list scan, compat DB) blocks for
            // seconds on low-end devices. Running it on the UI thread stalls
            // Choreographer/HWUI (syncAndDrawFrame) and trips the Vitals ANR
            // grouped under condition_variable::wait. Pre-warm off-thread;
            // ensureLoaded() is synchronized/idempotent so IO-thread callers
            // (launch/install) safely join the same init.
            val app = this
            Thread({
                runCatching { NativeLibraryLoader.ensureLoaded(app) }
            }, "EmuCoreV-Init").apply { isDaemon = true; start() }
        }
    }

    private fun recoverPendingDriveRestore() {
        if (!DriveBackupArchive.hasPendingRecovery(this)) return
        Thread({
            runBlocking {
                try {
                    BackupSessionGate.whileStopped { DriveBackupArchive(this@EmuCoreVApp).recoverPending() }
                } catch (cancelled: CancellationException) {
                    throw cancelled
                } catch (_: Exception) {
                    // The next create/restore call retries the same recovery under the gate.
                }
            }
        }, "EmuCoreV-DriveRecovery").apply { isDaemon = true; start() }
    }

    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)
        if (NativeLibraryLoader.isNativeSessionInitialized()) {
            NativeLib.onTrimMemory(level)
        }
    }

    override fun onLowMemory() {
        super.onLowMemory()
        if (NativeLibraryLoader.isNativeSessionInitialized()) {
            NativeLib.onTrimMemory(80)
        }
    }

    private companion object {
        const val PHOENIX_PROCESS_SUFFIX = ":phoenix"
        const val DISCORD_PROCESS_SUFFIX = ":discord"
    }
}
