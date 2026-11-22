package com.sbro.emucorev

import android.app.Application
import com.sbro.emucorev.core.AppStartup
import com.sbro.emucorev.core.BackupSessionGate
import com.sbro.emucorev.core.NativeLib
import com.sbro.emucorev.core.AppIconManager
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.core.NativeLibraryLoader
import com.sbro.emucorev.core.PlayTimeRepository
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.ProfilePlayTimeSyncer
import com.sbro.emucorev.data.TrophyCloudRepository
import com.sbro.emucorev.data.drive.DriveBackupArchive
import com.sbro.emucorev.data.drive.DriveBackupWork
import com.sbro.emucorev.discord.DiscordIntegration
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.runBlocking

class EmuCoreVApp : Application() {
    override fun onCreate() {
        super.onCreate()
        // ProcessPhoenix restarts the app through a helper process where Firebase providers
        // are not initialized, and the Discord SDK runs in its own isolated process; neither
        // helper may build the emulator-side graph.
        if (skipsAppStartup(Application.getProcessName())) return
        TrophyCloudRepository.resumePending(this)
        recoverPendingDriveRestore()
        DriveBackupWork.resumePending(this)
        DiscordIntegration.initialize(this)
        // Disk-heavy work (icon toggle, play-time recovery, storage skeleton and orphan purge,
        // defaults persistence) stays off the main thread. Consumers that depend on it (native
        // init, the Emulator activity starting a play-time session) call AppStartup.awaitReady().
        val app = this
        val prewarmNative = AppPreferences(this).onboardingCompleted
        AppStartup.begin {
            runCatching { AppIconManager.applyProIcon(app, AppPreferences(app).proUnlocked) }
            runCatching {
                PlayTimeRepository(app).finishOpenSessions().forEach { session ->
                    ProfilePlayTimeSyncer.recordAndSync(app, session.titleId, session.title, session.durationMs)
                }
            }
            runCatching { ProfilePlayTimeSyncer.syncPendingAsync(app) }
            runCatching {
                EmulatorStorage.prepareRuntime(app)
                VitaCoreConfigRepository(app).ensureDefaultsPersisted()
            }
            if (prewarmNative) {
                // Native init (Vulkan enumeration, app-list scan, compat DB) blocks for
                // seconds on low-end devices. Running it on the UI thread stalls
                // Choreographer/HWUI (syncAndDrawFrame) and trips the Vitals ANR
                // grouped under condition_variable::wait. Pre-warm off-thread;
                // ensureLoaded() is synchronized/idempotent so IO-thread callers
                // (launch/install) safely join the same init.
                Thread({
                    runCatching { NativeLibraryLoader.ensureLoaded(app) }
                }, "EmuCoreV-Init").apply { isDaemon = true; start() }
            }
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

    internal companion object {
        const val PHOENIX_PROCESS_SUFFIX = ":phoenix"
        const val DISCORD_PROCESS_SUFFIX = ":discord"
        const val NATIVE_SHELL_PROCESS_SUFFIX = ":native_shell"
        const val NATIVE_LIBRARY_PROCESS_SUFFIX = ":native_library"
        const val NATIVE_TROPHIES_PROCESS_SUFFIX = ":native_trophies"

        /**
         * Helper processes never build the emulator-side graph. ProcessPhoenix restarts the app
         * through a process where Firebase providers are not initialized, and the Discord SDK
         * runs in its own isolated process. The firmware UI processes have their own guest/SDL
         * runtime, and the resident native shell only browses files: none of them may close the
         * running game's play session, sync cloud data, start Discord, run backup or maintenance
         * work, or load the emulation core.
         */
        fun skipsAppStartup(processName: String?): Boolean {
            if (processName == null) return false
            return processName.endsWith(PHOENIX_PROCESS_SUFFIX) ||
                processName.endsWith(DISCORD_PROCESS_SUFFIX) ||
                processName.endsWith(NATIVE_SHELL_PROCESS_SUFFIX) ||
                processName.endsWith(NATIVE_LIBRARY_PROCESS_SUFFIX) ||
                processName.endsWith(NATIVE_TROPHIES_PROCESS_SUFFIX)
        }
    }
}
