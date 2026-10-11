package com.sbro.emucorev.core

import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import android.os.Build
import android.os.Debug
import android.os.PowerManager
import android.os.SystemClock
import androidx.annotation.Keep
import java.io.File
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONObject

data class NativePerformanceStats(
    val fps: Int? = null,
    val frameMs: Int? = null,
    val gpu: String? = null,
    val backend: String? = null,
    val core: String? = null,
    val width: Int? = null,
    val height: Int? = null,
    val paused: Boolean = false
)

data class PerformanceStatsSample(
    val native: NativePerformanceStats = NativePerformanceStats(),
    val cpuPercent: Double? = null,
    val cpuModel: String? = null,
    val gpuPercent: Double? = null,
    val memoryMiB: Double? = null,
    val batteryCelsius: Double? = null,
    val thermalStatus: Int? = null
)

internal fun parseNativePerformanceStats(text: String): NativePerformanceStats = runCatching {
    val json = JSONObject(text)
    fun positive(key: String) = (json.opt(key) as? Number)?.toLong()
        ?.takeIf { it in 1..Int.MAX_VALUE }?.toInt()
    fun name(key: String) = (json.opt(key) as? String)?.trim()?.takeIf { it.isNotEmpty() }
    NativePerformanceStats(positive("fps"), positive("frameMs"), parseGpuModelName(name("gpu")), name("backend"), name("core"),
        positive("width"), positive("height"), json.optBoolean("paused", false))
}.getOrDefault(NativePerformanceStats())

/** App CPU time divided by elapsed time and logical core count; not a guest CPU usage estimate. */
internal class ProcessCpuMeter(private val cores: Int) {
    private var previous: Pair<Long, Long>? = null
    fun sample(wallMs: Long, processMs: Long): Double? {
        val old = previous
        previous = wallMs to processMs
        if (old == null || wallMs <= old.first || processMs < old.second || cores <= 0) return null
        return ((processMs - old.second).toDouble() / (wallMs - old.first) / cores * 100).coerceIn(0.0, 100.0)
    }
}

/** KGSL reports busy and total for its last interval, not cumulative process counters. */
internal fun parseKgslBusyPercent(text: String): Double? {
    val parts = text.trim().split(Regex("\\s+"))
    if (parts.size != 2) return null
    val busy = parts[0].toLongOrNull() ?: return null
    val total = parts[1].toLongOrNull() ?: return null
    if (total <= 0 || busy < 0 || busy > total) return null
    return busy.toDouble() / total * 100
}

@Keep
object PerformanceStatsBridge {
    @Keep private external fun nativeSnapshot(): String
    private val calls = Mutex()

    // The native lifecycle mutex can wait behind Save/Load. Never acquire it on the UI thread.
    suspend fun snapshot(): NativePerformanceStats = withContext(Dispatchers.IO) {
        calls.withLock { parseNativePerformanceStats(runCatching { nativeSnapshot() }.getOrDefault("{}")) }
    }
}

class PerformanceStatsSampler(context: Context) {
    private val context = context.applicationContext
    private val cpu = ProcessCpuMeter(Runtime.getRuntime().availableProcessors())
    private val cpuModel = MobileSocNameMapper.currentDeviceName()
    private val gpuBusy = File("/sys/class/kgsl/kgsl-3d0/gpubusy")

    suspend fun sample(): PerformanceStatsSample = withContext(Dispatchers.IO) {
        val native = PerformanceStatsBridge.snapshot()
        val memory = runCatching { Debug.MemoryInfo().also(Debug::getMemoryInfo).totalPss.takeIf { it > 0 }?.div(1024.0) }.getOrNull()
        val battery = runCatching { context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED)) }
            .getOrNull()?.takeIf { it.hasExtra(BatteryManager.EXTRA_TEMPERATURE) }
            ?.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0)?.div(10.0)?.takeIf { it in -40.0..120.0 }
        val thermal = if (Build.VERSION.SDK_INT >= 29) runCatching {
            context.getSystemService(PowerManager::class.java)?.currentThermalStatus?.takeIf { it in 0..6 }
        }.getOrNull() else null
        // Read only this documented KGSL interface. Other GPU drivers/denied access remain unavailable.
        val gpu = runCatching { gpuBusy.bufferedReader().use { it.readLine()?.let(::parseKgslBusyPercent) } }.getOrNull()
        PerformanceStatsSample(native, cpu.sample(SystemClock.elapsedRealtime(), android.os.Process.getElapsedCpuTime()),
            cpuModel, gpu, memory, battery, thermal)
    }
}
