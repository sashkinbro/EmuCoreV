package com.sbro.emucorev.core

import android.app.ActivityManager
import android.app.ApplicationExitInfo
import android.content.Context
import android.os.Build
import android.util.Log
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@androidx.annotation.Keep
object AndroidDiagnostics {
    private var applicationContext: Context? = null

    fun initialize(context: Context) {
        applicationContext = context.applicationContext
    }


    private const val MIB = 1024L * 1024L

    @JvmStatic
    fun logStartupDiagnostics() {
        try {
            val app = applicationContext ?: return
            val text = buildString {
                appendMemoryState(app, this)
                appendExitReasons(app, this)
            }
            if (text.isNotEmpty())
                NativeLib.logDiagnostics(text)
        } catch (t: Throwable) {
            Log.e("EmuCoreV", "logStartupDiagnostics failed", t)
            try {
                NativeLib.logDiagnostics("startup diagnostics FAILED: $t")
            } catch (ignored: Throwable) {
            }
        }
    }

    private fun appendMemoryState(context: Context, sb: StringBuilder) {
        val am = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val info = ActivityManager.MemoryInfo()
        am.getMemoryInfo(info)
        sb.append("device memory: total=").append(info.totalMem / MIB)
            .append(" MiB avail=").append(info.availMem / MIB)
            .append(" MiB lmkThreshold=").append(info.threshold / MIB)
            .append(" MiB lowMemory=").append(info.lowMemory)
            .append(" lowRamDevice=").append(am.isLowRamDevice)
            .append(" memoryClass=").append(am.memoryClass).append('/').append(am.largeMemoryClass).append(" MB")
            .append('\n')
    }

    private fun appendTombstoneSummary(exit: ApplicationExitInfo, sb: StringBuilder) {
        if (Build.VERSION.SDK_INT < 31) return
        if (exit.reason != ApplicationExitInfo.REASON_CRASH_NATIVE) return
        try {
            val stream = exit.traceInputStream ?: run {
                sb.append("  tombstone: not available\n"); return
            }
            // Bound diagnostics memory even if Android exposes a very large tombstone.
            val bytes = stream.use {
                val buffer = ByteArray(256 * 1024)
                var size = 0
                while (size < buffer.size) {
                    val count = it.read(buffer, size, buffer.size - size)
                    if (count <= 0) break
                    size += count
                }
                buffer.copyOf(size)
            }
            val runs = ArrayList<String>()
            val cur = StringBuilder()
            for (b in bytes) {
                val c = b.toInt().toChar()
                if (c in ' '..'~') cur.append(c)
                else {
                    if (cur.length >= 8) runs.add(cur.toString())
                    cur.setLength(0)
                }
            }
            if (cur.length >= 8) runs.add(cur.toString())
            sb.append("  tombstone strings (").append(runs.size).append(" total, first 60):\n")
            for (r in runs.take(60)) sb.append("    ").append(r.take(200)).append('\n')
        } catch (e: Exception) {
            sb.append("  tombstone: read failed: ").append(e).append('\n')
        }
    }

    private fun appendExitReasons(context: Context, sb: StringBuilder) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            sb.append("previous exit reasons: unavailable below Android 11 (API ")
                .append(Build.VERSION.SDK_INT).append(")\n")
            return
        }
        val am = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val exits = am.getHistoricalProcessExitReasons(context.packageName, 0, 8)
        if (exits.isEmpty()) {
            sb.append("previous exit reasons: none recorded\n")
            return
        }
        val fmt = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US)
        for (exit in exits) {
            sb.append("previous exit: ").append(fmt.format(Date(exit.timestamp)))
                .append(" reason=").append(reasonName(exit.reason))
                .append(" status=").append(exit.status)
                .append(" importance=").append(exit.importance)
                .append(" rssAtDeath=").append(exit.rss / 1024).append(" MiB")
                .append(" pssAtDeath=").append(exit.pss / 1024).append(" MiB")
                .append(" desc=").append(exit.description ?: "-")
                .append('\n')
            appendTombstoneSummary(exit, sb)
        }
    }

    private fun reasonName(reason: Int): String = when (reason) {
        ApplicationExitInfo.REASON_EXIT_SELF -> "EXIT_SELF"
        ApplicationExitInfo.REASON_SIGNALED -> "SIGNALED"
        ApplicationExitInfo.REASON_LOW_MEMORY -> "LOW_MEMORY(LMK)"
        ApplicationExitInfo.REASON_CRASH -> "JAVA_CRASH"
        ApplicationExitInfo.REASON_CRASH_NATIVE -> "NATIVE_CRASH"
        ApplicationExitInfo.REASON_ANR -> "ANR"
        ApplicationExitInfo.REASON_INITIALIZATION_FAILURE -> "INIT_FAILURE"
        ApplicationExitInfo.REASON_PERMISSION_CHANGE -> "PERMISSION_CHANGE"
        ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE -> "EXCESSIVE_RESOURCE_USAGE"
        ApplicationExitInfo.REASON_USER_REQUESTED -> "USER_REQUESTED(swipe/force-stop)"
        ApplicationExitInfo.REASON_USER_STOPPED -> "USER_STOPPED"
        ApplicationExitInfo.REASON_DEPENDENCY_DIED -> "DEPENDENCY_DIED"
        ApplicationExitInfo.REASON_OTHER -> "OTHER"
        ApplicationExitInfo.REASON_FREEZER -> "FREEZER"
        else -> "UNKNOWN($reason)"
    }
}
