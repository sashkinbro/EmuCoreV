package com.sbro.emucorev.core

import android.content.Context
import android.content.Intent
import android.util.Log
import android.system.Os
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
        Failure
    }

    suspend fun launchInstalledTitle(context: Context, titleId: String): LaunchResult {
        if (!launchInFlight.compareAndSet(false, true)) return LaunchResult.Success
        preparingLaunch.value = true
        val startedAt = SystemClock.elapsedRealtime()
        try {
            return withContext(Dispatchers.IO) {
                if (!EmulatorStorage.hasInstalledFirmware(context)) return@withContext LaunchResult.MissingFirmware
                if (!EmulatorStorage.hasInstalledFirmwareUpdate(context)) return@withContext LaunchResult.MissingFirmwareUpdate
                val config = VitaGameSettingsRepository(context).syncEffectiveDriverForLaunch(titleId)
                val shouldAngle = config.useAngle && config.backendRenderer == "OpenGL"
                launchWithArgs(context, "LAUNCH_$titleId", arrayOf("-r", titleId), shouldAngle)
                LaunchResult.Success
            }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (error: Exception) {
            Log.e(TAG, "Failed to prepare game $titleId", error)
            return LaunchResult.Failure
        } finally {
            Log.i(TAG, "Launch preparation for $titleId took ${SystemClock.elapsedRealtime() - startedAt} ms")
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

    private suspend fun launchWithArgs(context: Context, action: String, args: Array<String>, useAngle: Boolean = false) {
        Log.i(TAG, "Launching emulator action=$action args=${args.joinToString(" ")}")
        applyAngleEnvironment(context, useAngle)
        NativeLibraryLoader.ensureLoaded(context)
        // Native init and installers maintain the app list; setup_game_launch
        // refreshes on a cache miss (e.g. a manually copied game).
        val intent = Intent(context, Emulator::class.java).apply {
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            putExtra(APP_RESTART_PARAMETERS, args)
            this.action = "${action}_${System.currentTimeMillis()}"
        }
        withContext(Dispatchers.Main.immediate) { context.startActivity(intent) }
    }

    private fun runWithArgs(context: Context, action: String, args: Array<String>): Boolean {
        return runCatching {
            Log.i(TAG, "Restarting emulator action=$action args=${args.joinToString(" ")}")
            applyAngleEnvironment(context, false)
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

    private fun applyAngleEnvironment(context: Context, useAngle: Boolean) {
        try {
            if (useAngle) {
                val libDir = context.applicationInfo.nativeLibraryDir
                val eglPath = "$libDir/libEGL_angle.so"
                val glPath = "$libDir/libGLESv2_angle.so"
                Log.i(TAG, "Enabling ANGLE. EGL: $eglPath, GL: $glPath")
                
                try {
                    System.load(glPath)
                    System.load(eglPath)
                    Log.i(TAG, "Successfully pre-loaded ANGLE libraries into memory.")
                } catch (e: Throwable) {
                    Log.e(TAG, "Failed to pre-load ANGLE libraries: ${e.message}", e)
                }

                Os.setenv("SDL_VIDEO_EGL_DRIVER", eglPath, true)
                Os.setenv("SDL_VIDEO_GL_DRIVER", glPath, true)
            } else {
                Log.i(TAG, "Disabling ANGLE.")
                Os.unsetenv("SDL_VIDEO_EGL_DRIVER")
                Os.unsetenv("SDL_VIDEO_GL_DRIVER")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to update ANGLE environment variables", e)
        }
    }
}
