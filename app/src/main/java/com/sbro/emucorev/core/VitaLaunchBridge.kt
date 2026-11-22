package com.sbro.emucorev.core

import android.content.Context
import android.content.Intent
import android.util.Log
import android.os.SystemClock
import com.jakewharton.processphoenix.ProcessPhoenix
import com.sbro.emucorev.core.vita.Emulator
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import java.util.concurrent.atomic.AtomicBoolean

object VitaLaunchBridge {
    private const val TAG = "VitaLaunchBridge"
    private const val APP_RESTART_PARAMETERS = "AppStartParameters"
    private const val ACTION_INSTALL_FIRMWARE = "INSTALL_FIRMWARE"
    private const val ACTION_INSTALL_CONTENT = "INSTALL_CONTENT"
    private const val ACTION_INSTALL_PKG = "INSTALL_PKG"
    private val launchInFlight = AtomicBoolean(false)
    private val preparingLaunch = MutableStateFlow(false)
    val isPreparingLaunch = preparingLaunch.asStateFlow()

    enum class LaunchResult {
        Success,
        MissingFirmware,
        MissingFirmwareUpdate,
        /** The selected storage root (for example an SD card) is not present. */
        StorageUnavailable,
        Failure,
        /** A previous launch has not reported ready yet; nothing was started. */
        Busy
    }

    suspend fun launchInstalledTitle(context: Context, titleId: String): LaunchResult {
        return launchInstalledTitle(context, titleId, null)
    }

    suspend fun launchInstalledTitleWithSaveState(context: Context, titleId: String, statePath: String): LaunchResult {
        return launchInstalledTitle(context, titleId, statePath)
    }

    /**
     * Callable from any process: it only sends the Intent. Core update, save-data migration,
     * driver sync, ANGLE environment, auto-load resolution and state validation all run in the
     * Emulator process (Emulator.main/getArguments), which owns the files and the SDL environment.
     * The cheap read-only firmware check stays here so the caller can report it immediately;
     * the Emulator repeats it for launches that do not come through this bridge.
     */
    private suspend fun launchInstalledTitle(context: Context, titleId: String, statePath: String?): LaunchResult {
        if (!launchInFlight.compareAndSet(false, true)) return LaunchResult.Busy
        preparingLaunch.value = true
        val startedAt = SystemClock.elapsedRealtime()
        val gate = LaunchGate.forContext(context)
        var gateHeld = false
        try {
            return withContext(Dispatchers.IO) {
                if (!gate.tryAcquire()) return@withContext LaunchResult.Busy
                gateHeld = true
                if (EmulatorStorage.isStorageUnavailable(context)) return@withContext LaunchResult.StorageUnavailable
                if (!EmulatorStorage.hasInstalledFirmware(context)) return@withContext LaunchResult.MissingFirmware
                if (!EmulatorStorage.hasInstalledFirmwareUpdate(context)) return@withContext LaunchResult.MissingFirmwareUpdate
                val returnToNative = (context is Emulator && context.returnsToNativeLibrary) ||
                    context is com.sbro.emucorev.core.vita.NativeLibraryMode.ReturnTarget
                val intent = LaunchIntents.buildGameLaunch(context, titleId, statePath, returnToNative,
                    gameSessionLive = GameSessionMarker.forContext(context).isLive())
                Log.i(TAG, "Launching emulator action=${intent.action} title=$titleId state=${statePath != null}")
                withContext(Dispatchers.Main.immediate) { context.startActivity(intent) }
                gateHeld = false // The Emulator process releases the gate once it is ready.
                LaunchResult.Success
            }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (error: Exception) {
            Log.e(TAG, "Failed to launch game $titleId", error)
            return LaunchResult.Failure
        } finally {
            if (gateHeld) gate.release()
            Log.i(TAG, "Launch dispatch for $titleId took ${SystemClock.elapsedRealtime() - startedAt} ms")
            preparingLaunch.value = false
            launchInFlight.set(false)
        }
    }

    fun installFirmware(context: Context, firmwarePath: String): Boolean {
        return runWithArgs(context, ACTION_INSTALL_FIRMWARE, arrayOf("--firmware", firmwarePath))
    }

    fun installContent(context: Context, contentPath: String): Boolean {
        return runWithArgs(context, ACTION_INSTALL_CONTENT, arrayOf(contentPath))
    }

    fun installPkg(context: Context, pkgPath: String, zrif: String): Boolean {
        return runWithArgs(context, ACTION_INSTALL_PKG, arrayOf("--pkg", pkgPath, "--zrif", zrif))
    }

    private fun runWithArgs(context: Context, action: String, args: Array<String>): Boolean {
        return runCatching {
            Log.i(TAG, "Restarting emulator action=$action args=${args.joinToString(" ")}")
            CoreMaintenanceRepository(context).ensureCoreUpdated()
            AngleEnvironment.apply(context, false)
            EmulatorStorage.prepareRuntime(context)
            VitaCoreConfigRepository(context).ensureDefaultsPersisted()
            NativeLibraryLoader.ensureLoaded(context)
            NativeLib.refreshAppsList()
            val intent = Intent(context, Emulator::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                putExtra(APP_RESTART_PARAMETERS, args)
                this.action = "${action}_${System.currentTimeMillis()}"
            }
            ProcessPhoenix.triggerRebirth(context.applicationContext, intent)
        }.onFailure { error ->
            Log.e(TAG, "Failed to restart emulator action=$action args=${args.joinToString(" ")}", error)
        }.isSuccess
    }
}
