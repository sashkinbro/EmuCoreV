package com.sbro.emucorev.core

import android.content.Context
import android.content.Intent
import com.sbro.emucorev.core.vita.Emulator

/**
 * The only thing a launch from any process does: describe which title (and optionally which
 * explicit save state) to boot. All preparation runs in the Emulator process.
 */
internal object LaunchIntents {
    const val APP_RESTART_PARAMETERS = "AppStartParameters"

    fun launchArguments(titleId: String, statePath: String?): Array<String> =
        if (statePath.isNullOrBlank()) arrayOf("-r", titleId) else arrayOf("-r", titleId, "-loadstate", statePath)

    /**
     * A fresh launch clears the task so a stale retained intent (for example a crashed
     * -loadstate launch) is never re-delivered. While a game session is live, the existing
     * singleInstance task must stay intact: it receives the Intent in onNewIntent and shuts the
     * running game down gracefully before the process is reborn.
     */
    fun buildGameLaunch(
        context: Context,
        titleId: String,
        statePath: String?,
        returnToNativeLibrary: Boolean,
        gameSessionLive: Boolean,
        nowMs: Long = System.currentTimeMillis()
    ): Intent = Intent(context, Emulator::class.java).apply {
        var launchFlags = Intent.FLAG_ACTIVITY_NEW_TASK
        if (!gameSessionLive) launchFlags = launchFlags or Intent.FLAG_ACTIVITY_CLEAR_TASK
        addFlags(launchFlags)
        putExtra(APP_RESTART_PARAMETERS, launchArguments(titleId, statePath))
        if (returnToNativeLibrary) putExtra(com.sbro.emucorev.core.vita.NativeLibraryMode.RETURN_TO_NATIVE_LIBRARY, true)
        action = "LAUNCH_${titleId}_$nowMs"
    }

    /**
     * An Intent restored by the system (recents, or the saved task after process death) is old:
     * its explicit or automatic state load must not run again. Returns true if it was stripped.
     */
    fun sanitizeRestoredLaunch(intent: Intent, restoredByFramework: Boolean): Boolean {
        val fromHistory = intent.flags and Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY != 0
        if (!restoredByFramework && !fromHistory) return false
        val args = intent.getStringArrayExtra(APP_RESTART_PARAMETERS) ?: return false
        if (!args.contains("-loadstate")) return false
        intent.putExtra(APP_RESTART_PARAMETERS, autoLoadFallbackArguments(args))
        intent.putExtra(SKIP_AUTOMATIC_STATE_LOAD_EXTRA, true)
        intent.putExtra(AUTOMATIC_STATE_LOAD_EXTRA, false)
        return true
    }
}
