package com.sbro.emucorev.core

import android.content.Context
import android.system.Os
import android.util.Log

/**
 * Everything a game launch needs before SDL starts. It runs only in the Emulator process,
 * so launches from the main app and from the resident native shell process behave identically
 * and use fresh settings (the shell process may have been alive for hours).
 */
internal object GameLaunchPreparation {
    private const val TAG = "GameLaunchPreparation"

    sealed interface Outcome {
        data class Ready(val useAngle: Boolean) : Outcome
        data object MissingFirmware : Outcome
        data object MissingFirmwareUpdate : Outcome
        data object Failed : Outcome
    }

    fun prepare(context: Context, titleId: String): Outcome {
        return try {
            CoreMaintenanceRepository(context).ensureCoreUpdated()
            if (!EmulatorStorage.hasInstalledFirmware(context)) return Outcome.MissingFirmware
            if (!EmulatorStorage.hasInstalledFirmwareUpdate(context)) return Outcome.MissingFirmwareUpdate
            // Saves imported by older builds sit in ux0:user/savedata, which
            // the mounted savedata0: never reads. Relocate them before the
            // game starts so transferred saves are actually picked up.
            runCatching { com.sbro.emucorev.data.SaveDataRepository().migrateLegacySaveData(context) }
                .onFailure { error -> Log.w(TAG, "Save data migration failed", error) }
            val settings = VitaGameSettingsRepository(context)
            ensureMeasuredGpuForCustomDriver(context, settings, titleId)
            val config = settings.syncEffectiveDriverForLaunch(titleId)
            val useAngle = config.useAngle && config.backendRenderer == "OpenGL"
            AngleEnvironment.apply(context, useAngle)
            Outcome.Ready(useAngle)
        } catch (error: Exception) {
            Log.e(TAG, "Launch preparation failed for $titleId", error)
            Outcome.Failed
        }
    }

    /**
     * A custom driver needs a measured GL_RENDERER; this fresh process has none unless the main app
     * persisted one for this build. Bounded; runs on the caller's launch thread, never the main thread.
     */
    private fun ensureMeasuredGpuForCustomDriver(
        context: Context, settings: VitaGameSettingsRepository, titleId: String
    ) {
        if (DeviceGpuInfoProvider.hasMeasured(context)) return
        if (settings.loadProfile(titleId).config.customDriverName.isBlank()) return
        runCatching {
            kotlinx.coroutines.runBlocking {
                kotlinx.coroutines.withTimeoutOrNull(GPU_PROBE_BUDGET_MS) {
                    DeviceGpuInfoProvider.ensureLoaded(context)
                }
            }
        }.onFailure { Log.w(TAG, "GPU probe before driver selection failed", it) }
    }

    private const val GPU_PROBE_BUDGET_MS = 2_000L
}

internal object AngleEnvironment {
    private const val TAG = "AngleEnvironment"

    /** Must run in the process that hosts SDL: the variables are read by its native video init. */
    fun apply(context: Context, useAngle: Boolean) {
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
