package com.sbro.emucorev.core

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log

/**
 * Debug-only lab input channel (tools/oracles/lab/LAUNCH_CONTRACT.md, "Lab input"):
 * `adb shell am broadcast -a com.sbro.emucorev.LAB_INPUT -p com.sbro.emucorev --es cmd "press cross 200"`.
 * Declared only in src/debug/AndroidManifest.xml, so release builds have neither the receiver nor this class.
 * The result of the command (or the error) is returned as the broadcast result data.
 */
class LabInputReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val cmd = intent.getStringExtra("cmd")?.trim().orEmpty()
        val result = if (cmd.isEmpty()) {
            "error: missing --es cmd"
        } else {
            try {
                LabBridge.nativeInput(cmd)
            } catch (t: Throwable) {
                // No game running in this process (native core not loaded) or a build without the oracle hooks.
                "error: native lab input unavailable (${t.javaClass.simpleName})"
            }
        }
        Log.w(LabSession.TAG, "lab_input broadcast cmd=\"$cmd\" -> $result")
        if (isOrderedBroadcast) setResult(if (result.startsWith("ok")) 1 else 2, result, null)
    }
}
