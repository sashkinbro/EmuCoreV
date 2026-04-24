package com.sbro.emucorev.core

import android.content.Context
import android.content.Intent
import com.jakewharton.processphoenix.ProcessPhoenix
import com.sbro.emucorev.core.vita.Emulator

object VitaLaunchBridge {
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
        return if (launchWithArgs(context, "LAUNCH_$titleId", arrayOf("-r", titleId))) {
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

    private fun launchWithArgs(context: Context, action: String, args: Array<String>): Boolean {
        return runCatching {
            EmulatorStorage.prepareRuntime(context)
            NativeLibraryLoader.ensureLoaded(context)
            VitaCoreConfigRepository(context).ensureDefaultsPersisted()
            val intent = Intent(context, Emulator::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                putExtra(APP_RESTART_PARAMETERS, args)
                this.action = "${action}_${System.currentTimeMillis()}"
            }
            context.startActivity(intent)
        }.isSuccess
    }

    private fun runWithArgs(context: Context, action: String, args: Array<String>): Boolean {
        return runCatching {
            EmulatorStorage.prepareRuntime(context)
            NativeLibraryLoader.ensureLoaded(context)
            VitaCoreConfigRepository(context).ensureDefaultsPersisted()
            val intent = Intent(context, Emulator::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                putExtra(APP_RESTART_PARAMETERS, args)
                this.action = "${action}_${System.currentTimeMillis()}"
            }
            ProcessPhoenix.triggerRebirth(context.applicationContext, intent)
        }.isSuccess
    }
}
