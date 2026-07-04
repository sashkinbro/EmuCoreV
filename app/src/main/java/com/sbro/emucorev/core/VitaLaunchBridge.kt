package com.sbro.emucorev.core

import android.content.Context
import android.content.Intent
import android.util.Log
import android.system.Os
import com.jakewharton.processphoenix.ProcessPhoenix
import com.sbro.emucorev.core.vita.Emulator

object VitaLaunchBridge {
    private const val TAG = "VitaLaunchBridge"
    private const val APP_RESTART_PARAMETERS = "AppStartParameters"
    private const val ACTION_INSTALL_FIRMWARE = "INSTALL_FIRMWARE"
    private const val ACTION_INSTALL_CONTENT = "INSTALL_CONTENT"
    private const val ACTION_INSTALL_PKG = "INSTALL_PKG"

    enum class LaunchResult {
        Success,
        MissingFirmware,
        MissingFirmwareUpdate,
        Failure
    }

    fun launchInstalledTitle(context: Context, titleId: String): LaunchResult {
        if (!EmulatorStorage.hasInstalledFirmware(context)) {
            return LaunchResult.MissingFirmware
        }
        if (!EmulatorStorage.hasInstalledFirmwareUpdate(context)) {
            return LaunchResult.MissingFirmwareUpdate
        }
        val gameSettingsRepo = VitaGameSettingsRepository(context)
        gameSettingsRepo.syncEffectiveDriverForLaunch(titleId)
        val config = gameSettingsRepo.loadEffective(titleId)
        val shouldAngle = config.useAngle && config.backendRenderer == "OpenGL"
        
        return if (launchWithArgs(context, "LAUNCH_$titleId", arrayOf("-r", titleId), shouldAngle)) {
            LaunchResult.Success
        } else {
            LaunchResult.Failure
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

    private fun launchWithArgs(context: Context, action: String, args: Array<String>, useAngle: Boolean = false): Boolean {
        return runCatching {
            Log.i(TAG, "Launching emulator action=$action args=${args.joinToString(" ")}")
            applyAngleEnvironment(context, useAngle)
            EmulatorStorage.prepareRuntime(context)
            VitaCoreConfigRepository(context).ensureDefaultsPersisted()
            NativeLibraryLoader.ensureLoaded(context)
            NativeLib.refreshAppsList()
            val intent = Intent(context, Emulator::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                putExtra(APP_RESTART_PARAMETERS, args)
                this.action = "${action}_${System.currentTimeMillis()}"
            }
            context.startActivity(intent)
        }.onFailure { error ->
            Log.e(TAG, "Failed to launch emulator action=$action args=${args.joinToString(" ")}", error)
        }.isSuccess
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
