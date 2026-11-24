package com.sbro.emucorev.core

import android.content.Context
import android.content.Intent
import android.os.SystemClock
import android.util.Log
import com.sbro.emucorev.BuildConfig
import java.io.File

/** Parsed `ecv.lab*` extras of a lab launch (tools/oracles/lab/LAUNCH_CONTRACT.md). */
internal data class LabConfig(
    val runId: String,
    val frameCapture: Int?,
    val gpuTrace: Boolean,
    val statsOverlay: Boolean,
    val perfIntervalSeconds: Int
) {
    companion object {
        const val EXTRA_LAB = "ecv.lab"
        const val EXTRA_RUN_ID = "ecv.lab.run_id"
        const val EXTRA_FRAME_CAPTURE = "ecv.lab.frame_capture"
        const val EXTRA_GPU_TRACE = "ecv.lab.gpu_trace"
        const val EXTRA_STATS_OVERLAY = "ecv.lab.stats_overlay"
        const val EXTRA_PERF_INTERVAL = "ecv.lab.perf_interval_s"
        const val DEFAULT_PERF_INTERVAL_S = 5
        const val MAX_FRAME_CAPTURE = 120 // the native capture hook caps at 120
        private val RUN_ID = Regex("[A-Za-z0-9._-]{1,64}")

        /** Null unless the intent asks for a lab launch with a well-formed run id. */
        fun fromIntent(intent: Intent?): LabConfig? {
            if (intent == null || !intent.getBooleanExtra(EXTRA_LAB, false)) return null
            val runId = intent.getStringExtra(EXTRA_RUN_ID)?.trim().orEmpty()
            if (!RUN_ID.matches(runId)) return null
            return LabConfig(
                runId = runId,
                frameCapture = if (intent.hasExtra(EXTRA_FRAME_CAPTURE))
                    intent.getIntExtra(EXTRA_FRAME_CAPTURE, 0).takeIf { it > 0 }?.coerceAtMost(MAX_FRAME_CAPTURE) else null,
                gpuTrace = intent.getBooleanExtra(EXTRA_GPU_TRACE, false),
                statsOverlay = intent.getBooleanExtra(EXTRA_STATS_OVERLAY, true),
                perfIntervalSeconds = intent.getIntExtra(EXTRA_PERF_INTERVAL, DEFAULT_PERF_INTERVAL_S)
                    .takeIf { it > 0 } ?: DEFAULT_PERF_INTERVAL_S
            )
        }
    }
}

/**
 * Debug-only lab launch contract. Release builds carry the no-op twin in src/release.
 * Nothing here persists a setting; the session lives and dies with the process.
 */
internal object LabSession {
    const val TAG = "ECVLAB"

    private var config: LabConfig? = null
    private var startedAtMs = 0L
    private var title = ""
    private var firstFrameLogged = false

    val active: Boolean get() = config != null

    /** False only when this lab session asked for the stats overlay off. */
    val statsOverlayAllowed: Boolean get() = config?.statsOverlay ?: true

    fun isLabIntent(intent: Intent?): Boolean = BuildConfig.DEBUG && LabConfig.fromIntent(intent) != null

    /** Activity.onCreate: stamps the launch time. A task restored by the system is never a lab launch. */
    fun begin(intent: Intent?, restored: Boolean) {
        config = null
        firstFrameLogged = false
        if (!BuildConfig.DEBUG || restored) return
        if (intent != null && intent.flags and Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY != 0) return
        config = LabConfig.fromIntent(intent)
        startedAtMs = SystemClock.elapsedRealtime()
    }

    /** SDLThread, after the launch request passed validation (native library is loaded). */
    fun accepted(titleId: String, args: Array<String>) {
        val cfg = config ?: return
        title = titleId
        val index = args.indexOf("-loadstate")
        val state = args.getOrNull(index + 1)?.takeIf { index >= 0 }.orEmpty()
        Log.i(TAG, "launch run=${cfg.runId} title=$titleId state=$state")
        runCatching { LabBridge.nativeConfigure(cfg.runId, titleId, cfg.perfIntervalSeconds, state.isNotEmpty()) }
            .onFailure { Log.w(TAG, "native lab hooks unavailable", it) }
    }

    /** Native render thread, the first presentation after a successful restore. */
    fun onStateFrameReady(context: Context) {
        val cfg = config ?: return
        val now = SystemClock.elapsedRealtime()
        if (firstFrameLogged) return
        firstFrameLogged = true
        Log.i(TAG, "first_frame run=${cfg.runId} ms_since_launch=${now - startedAtMs}")
        // Created after the first restored frame so the capture starts inside the restored scene.
        val dir = context.getExternalFilesDir(null) ?: return
        runCatching {
            cfg.frameCapture?.let { File(dir, "frame-capture").writeText(it.toString()) }
            if (cfg.gpuTrace) File(dir, "gpu-trace").writeText("")
        }.onFailure { Log.w(TAG, "could not create oracle flag files", it) }
    }

    fun onDestroy() {
        val cfg = config ?: return
        Log.i(TAG, "exit run=${cfg.runId}")
        config = null
    }
}
