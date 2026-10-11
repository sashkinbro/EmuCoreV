package com.sbro.emucorev.data

import android.content.Context
import java.io.Closeable
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

enum class StatsPosition { TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT }
enum class StatsMode { OFF, FPS, DETAILED }

internal fun legacyStatsMode(enabled: Boolean, detail: Int): StatsMode =
    if (!enabled) StatsMode.OFF else if (detail == 0) StatsMode.FPS else StatsMode.DETAILED

internal fun legacyStatsPosition(position: Int): StatsPosition = when (position) {
    0 -> StatsPosition.TOP_LEFT
    3 -> StatsPosition.BOTTOM_LEFT
    4, 5 -> StatsPosition.BOTTOM_RIGHT
    else -> StatsPosition.TOP_RIGHT
}

enum class StatsMetric(val bit: Int) {
    VERSION(1), FPS(2), FRAME_TIME(4), CPU(8), CPU_MODEL(16), GPU_MODEL(32),
    GPU_USAGE(64), MEMORY(128), TEMPERATURE(256), THERMAL(512), RESOLUTION(1024);

    val selectionMask: Int get() = when (this) {
        CPU -> bit or CPU_MODEL.bit
        GPU_MODEL -> bit or GPU_USAGE.bit
        else -> bit
    }
    companion object {
        val selectable = entries.filterNot { it == CPU_MODEL || it == GPU_USAGE }
        val allMask: Int = entries.fold(0) { mask, metric -> mask or metric.bit } }
}

data class PerformanceStatsSettings(
    val mode: StatsMode = StatsMode.OFF,
    val position: StatsPosition = StatsPosition.TOP_RIGHT,
    val scalePercent: Int = 100,
    val rememberedMode: StatsMode = StatsMode.DETAILED,
    val metricMask: Int = StatsMetric.VERSION.bit or StatsMetric.FPS.bit or StatsMetric.FRAME_TIME.bit or
        StatsMetric.CPU.bit or StatsMetric.CPU_MODEL.bit or StatsMetric.GPU_MODEL.bit or
        StatsMetric.GPU_USAGE.bit or StatsMetric.MEMORY.bit or StatsMetric.RESOLUTION.bit
) {
    val enabled: Boolean get() = mode != StatsMode.OFF
    fun shows(metric: StatsMetric): Boolean = metricMask and metric.bit != 0
    fun normalized() = copy(scalePercent = scalePercent.coerceIn(75, 200),
        rememberedMode = if (mode != StatsMode.OFF) mode else rememberedMode.takeUnless { it == StatsMode.OFF } ?: StatsMode.DETAILED,
        metricMask = (metricMask and StatsMetric.allMask).let { mask ->
            var grouped = mask
            if (mask and StatsMetric.CPU.selectionMask != 0) grouped = grouped or StatsMetric.CPU.selectionMask
            if (mask and StatsMetric.GPU_MODEL.selectionMask != 0) grouped = grouped or StatsMetric.GPU_MODEL.selectionMask
            grouped
        })
}

/** Global defaults and full per-title snapshots; absent game overrides inherit live defaults. */
class PerformanceStatsPreferences(context: Context, titleId: String? = null) : Closeable {
    // Length-prefixing keeps arbitrary homebrew title keys unambiguous in one file.
    private val prefix = titleId?.trim()?.takeIf { it.isNotEmpty() }?.let { "game:${it.length}:$it:" }.orEmpty()
    private fun key(name: String) = prefix + name
    private val prefs = context.applicationContext.getSharedPreferences("performance_stats", Context.MODE_PRIVATE)
    private val mutableSettings = MutableStateFlow(PerformanceStatsSettings())
    val settings: StateFlow<PerformanceStatsSettings> = mutableSettings.asStateFlow()
    private val listener = android.content.SharedPreferences.OnSharedPreferenceChangeListener { _, _ ->
        synchronized(writeLock) { mutableSettings.value = read() }
    }

    init {
        synchronized(writeLock) {
            if (!prefs.contains("mode") && prefs.all["enabled"] is Boolean) {
                prefs.edit().putString("mode", if (prefs.getBoolean("enabled", false)) StatsMode.DETAILED.name else StatsMode.OFF.name)
                    .remove("enabled").apply()
            }
            // Only the old prototype's exact default is upgraded. Explicit metric
            // choices survive; removing the obsolete keys makes this migration one-shot.
            if (prefs.contains("text_opacity") || prefs.contains("background_opacity")) {
                val edit = prefs.edit().remove("text_opacity").remove("background_opacity")
                if (prefs.all["metrics"] == 175) edit.putInt("metrics", PerformanceStatsSettings().metricMask)
                edit.apply()
            }
            prefs.registerOnSharedPreferenceChangeListener(listener)
            mutableSettings.value = read()
        }
    }

    fun update(transform: (PerformanceStatsSettings) -> PerformanceStatsSettings) {
        synchronized(writeLock) {
            val next = transform(read()).normalized()
            prefs.edit().putString(key("mode"), next.mode.name).remove(key("enabled"))
                .putInt(key("position"), next.position.ordinal).putInt(key("scale"), next.scalePercent)
                .putString(key("remembered_mode"), next.rememberedMode.name)
                .remove(key("text_opacity")).remove(key("background_opacity"))
                .putInt(key("metrics"), next.metricMask).apply()
            mutableSettings.value = next
        }
    }

    fun reset() {
        synchronized(writeLock) {
            if (prefix.isEmpty()) {
                update { PerformanceStatsSettings() }
            } else {
                val edit = prefs.edit()
                listOf("mode", "position", "scale", "remembered_mode", "metrics").forEach { edit.remove(key(it)) }
                edit.apply()
                mutableSettings.value = read()
            }
        }
    }

    fun setEnabled(enabled: Boolean) = update { current ->
        current.copy(mode = if (enabled) current.rememberedMode else StatsMode.OFF)
    }

    fun selectMode(mode: StatsMode) {
        require(mode != StatsMode.OFF)
        update { current -> current.copy(mode = if (current.enabled) mode else StatsMode.OFF, rememberedMode = mode) }
    }

    fun migrateLegacyCore(values: Map<String, String>) {
        if (prefix.isNotEmpty()) return
        val enabled = values["performance-overlay"]?.toBooleanStrictOrNull() ?: return
        synchronized(writeLock) {
            if (prefs.contains("mode")) return
            update { it.copy(mode = legacyStatsMode(enabled, values["performance-overlay-detail"]?.toIntOrNull() ?: 0),
                position = values["performance-overlay-position"]?.toIntOrNull()?.let(::legacyStatsPosition) ?: it.position) }
        }
    }

    private fun read(): PerformanceStatsSettings {
        val values = prefs.all
        val global = readValues(values, "", PerformanceStatsSettings())
        return if (prefix.isEmpty() || !values.containsKey(key("mode"))) global
            else readValues(values, prefix, global)
    }

    private fun readValues(values: Map<String, *>, scope: String, defaults: PerformanceStatsSettings): PerformanceStatsSettings =
        defaults.copy(mode = StatsMode.entries.firstOrNull { it.name == values[scope + "mode"] } ?: defaults.mode,
            position = StatsPosition.entries.getOrNull(values[scope + "position"] as? Int ?: -1) ?: defaults.position,
            scalePercent = values[scope + "scale"] as? Int ?: defaults.scalePercent,
            rememberedMode = StatsMode.entries.firstOrNull { it.name == values[scope + "remembered_mode"] } ?: defaults.rememberedMode,
            metricMask = values[scope + "metrics"] as? Int ?: defaults.metricMask).normalized()

    override fun close() { prefs.unregisterOnSharedPreferenceChangeListener(listener) }

    companion object { private val writeLock = Any() }
}
