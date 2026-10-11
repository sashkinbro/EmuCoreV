package com.sbro.emucorev.ui.emulation

import androidx.compose.foundation.border
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.material3.Text
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.sbro.emucorev.BuildConfig
import com.sbro.emucorev.R
import com.sbro.emucorev.core.PerformanceStatsSample
import com.sbro.emucorev.core.PerformanceStatsSampler
import com.sbro.emucorev.data.PerformanceStatsPreferences
import com.sbro.emucorev.data.PerformanceStatsSettings
import com.sbro.emucorev.data.StatsMode
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.withStyle
import com.sbro.emucorev.data.StatsMetric
import com.sbro.emucorev.data.StatsPosition
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import java.util.Locale

@Composable
fun statsMetricLabel(metric: StatsMetric): String = stringResource(when (metric) {
    StatsMetric.VERSION -> R.string.stats_version
    StatsMetric.FPS -> R.string.stats_fps
    StatsMetric.FRAME_TIME -> R.string.stats_frame_time
    StatsMetric.CPU -> R.string.stats_cpu
    StatsMetric.CPU_MODEL -> R.string.stats_cpu_model
    StatsMetric.GPU_MODEL -> R.string.stats_gpu_model
    StatsMetric.GPU_USAGE -> R.string.stats_gpu_usage
    StatsMetric.MEMORY -> R.string.stats_memory
    StatsMetric.TEMPERATURE -> R.string.stats_temperature
    StatsMetric.THERMAL -> R.string.stats_thermal
    StatsMetric.RESOLUTION -> R.string.stats_resolution
})

@Composable
fun PerformanceStatsOverlay(visible: Boolean, modifier: Modifier = Modifier, titleId: String? = null) {
    val context = LocalContext.current
    val preferences = remember(context, titleId) { PerformanceStatsPreferences(context, titleId) }
    val settings by preferences.settings.collectAsState()
    var sample by remember { mutableStateOf(PerformanceStatsSample()) }
    DisposableEffect(preferences) { onDispose { preferences.close() } }
    LaunchedEffect(preferences) {
        preferences.settings.map { it.enabled }.distinctUntilChanged().collectLatest { enabled ->
            if (enabled) {
                val sampler = PerformanceStatsSampler(context)
                while (true) { sample = sampler.sample(); delay(1000) }
            }
        }
    }
    if (!visible || !settings.enabled) return
    PerformanceStatsHud(settings, sample, modifier)
}

@Composable
internal fun PerformanceStatsHud(settings: PerformanceStatsSettings, sample: PerformanceStatsSample, modifier: Modifier = Modifier) {
    if (!settings.enabled || (settings.mode == StatsMode.DETAILED && settings.metricMask == 0)) return
    val right = settings.position == StatsPosition.TOP_RIGHT || settings.position == StatsPosition.BOTTOM_RIGHT
    val alignment = when (settings.position) {
        StatsPosition.TOP_LEFT -> Alignment.TopStart
        StatsPosition.TOP_RIGHT -> Alignment.TopEnd
        StatsPosition.BOTTOM_LEFT -> Alignment.BottomStart
        StatsPosition.BOTTOM_RIGHT -> Alignment.BottomEnd
    }
    val unavailable = stringResource(R.string.stats_unavailable)
    val paused = stringResource(R.string.stats_paused)
    val thermal = sample.thermalStatus?.let { status -> stringResource(when (status) {
        0 -> R.string.stats_thermal_none
        1 -> R.string.stats_thermal_light
        2 -> R.string.stats_thermal_moderate
        3 -> R.string.stats_thermal_severe
        4 -> R.string.stats_thermal_critical
        5 -> R.string.stats_thermal_emergency
        else -> R.string.stats_thermal_shutdown
    }) }
    fun number(value: Double?, suffix: String) = value?.let { String.format(Locale.ROOT, "%.1f%s", it, suffix) } ?: unavailable
    BoxWithConstraints(modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing).padding(12.dp), contentAlignment = alignment) {
        val opacity = 1f
        if (settings.mode == StatsMode.FPS) {
            val scale = settings.scalePercent / 100f
            Box(Modifier.testTag("performance_stats_panel")
                .background(Color.Black.copy(alpha = 0.42f), RoundedCornerShape(999.dp))
                .border(1.dp, Color.White.copy(alpha = 0.08f), RoundedCornerShape(999.dp))
                .padding(horizontal = (10 * scale).dp, vertical = (5 * scale).dp)) {
            Row(Modifier.testTag("stats_metric_fps"), verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Text(stringResource(R.string.stats_fps), color = Color.White.copy(alpha = 0.72f * opacity),
                    style = androidx.compose.material3.MaterialTheme.typography.labelSmall.copy(
                        fontSize = (11 * scale).sp, lineHeight = (14 * scale).sp,
                        fontWeight = FontWeight.SemiBold, letterSpacing = 0.2.sp))
                Text(if (sample.native.paused) paused else sample.native.fps?.toString() ?: unavailable,
                    color = (if ((sample.native.fps ?: 0) >= 55) Color(0xFF50D9A0) else Color(0xFFFFB85C)).copy(alpha = opacity),
                    style = androidx.compose.material3.MaterialTheme.typography.labelLarge.copy(
                        fontSize = (14 * scale).sp, lineHeight = (18 * scale).sp,
                        fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold))
            }
        }
        } else {
        val footer = setOf(StatsMetric.FRAME_TIME, StatsMetric.RESOLUTION)
        val metrics = StatsMetric.selectable.filter(settings::shows).sortedBy { if (it in footer) 1 else 0 }
        val detailedStyle = androidx.compose.material3.MaterialTheme.typography.labelMedium
        val density = LocalDensity.current
        val textMeasurer = rememberTextMeasurer()
        // SP conversion, actual font metrics and pixel rounding all matter on short screens.
        // Measure the same one-line styles rather than treating the requested line height
        // as the font's measured height (which can be taller at large system font scales).
        fun measuredHeight(candidate: Float): Int {
            val rows = metrics.sumOf { metric ->
                val small = metric in footer
                textMeasurer.measure(AnnotatedString("Ag"), style = detailedStyle.copy(
                    letterSpacing = 0.sp,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = if (small) FontWeight.Medium else FontWeight.Bold,
                    fontSize = ((if (small) 8.5f else 10f) * candidate).sp,
                    lineHeight = ((if (small) 11f else 13f) * candidate).sp),
                    softWrap = false, maxLines = 1).size.height
            }
            return with(density) {
                rows + 2 * (4 * candidate).dp.roundToPx() +
                    (metrics.size - 1).coerceAtLeast(0) * candidate.dp.roundToPx() +
                    if (metrics.any { it in footer }) (4 * candidate).dp.roundToPx() else 0
            }
        }
        val requestedScale = settings.scalePercent / 100f
        val availableHeight = with(density) { maxHeight.roundToPx() }
        val scale = remember(metrics, requestedScale, maxHeight, density.density, density.fontScale, textMeasurer, detailedStyle) {
            if (measuredHeight(requestedScale) <= availableHeight) requestedScale else {
                var low = 0.01f
                var high = requestedScale
                repeat(16) {
                    val middle = (low + high) / 2
                    if (measuredHeight(middle) <= availableHeight) low = middle else high = middle
                }
                low
            }
        }
        Column(Modifier.widthIn(min = 190.dp, max = (330 * (settings.scalePercent / 100f).coerceAtMost(1.75f)).dp)
            .testTag("performance_stats_panel")
            .padding(horizontal = (6 * scale).dp, vertical = (4 * scale).dp),
            verticalArrangement = Arrangement.spacedBy((1 * scale).dp),
            horizontalAlignment = if (right) Alignment.End else Alignment.Start) {
            var footerStarted = false
            for (metric in metrics) {
                val inFooter = metric in footer
                val firstFooter = inFooter && !footerStarted
                if (inFooter) footerStarted = true
                val value = when (metric) {
                    StatsMetric.VERSION -> "EmuCoreV ${BuildConfig.VERSION_NAME} | ${sample.native.core ?: "Vita3K"}"
                    StatsMetric.FPS -> if (sample.native.paused) paused else sample.native.fps?.toString() ?: unavailable
                    StatsMetric.FRAME_TIME -> if (sample.native.paused) paused else sample.native.frameMs?.let { "$it ms" } ?: unavailable
                    StatsMetric.CPU -> listOfNotNull(sample.cpuModel, sample.cpuPercent?.let { number(it, "%") }).joinToString(" · ").ifEmpty { unavailable }
                    StatsMetric.CPU_MODEL -> sample.cpuModel ?: unavailable
                    StatsMetric.GPU_MODEL -> listOfNotNull(sample.native.gpu, sample.gpuPercent?.let { number(it, "%") }).joinToString(" · ").ifEmpty { unavailable }
                    StatsMetric.GPU_USAGE -> number(sample.gpuPercent, "%")
                    StatsMetric.MEMORY -> number(sample.memoryMiB, " MiB")
                    StatsMetric.TEMPERATURE -> number(sample.batteryCelsius, " °C")
                    StatsMetric.THERMAL -> thermal ?: unavailable
                    StatsMetric.RESOLUTION -> sample.native.width?.let { w -> sample.native.height?.let { h -> "${sample.native.backend?.let { "$it · " }.orEmpty()}$w × $h" } } ?: unavailable
                }
                Text(text = buildAnnotatedString {
                    if (metric != StatsMetric.VERSION) {
                        withStyle(SpanStyle(color = Color(0xFF9DD7FF).copy(alpha = opacity))) { append("${when (metric) { StatsMetric.TEMPERATURE -> "Temp"; StatsMetric.THERMAL -> "Thermal"; else -> statsMetricLabel(metric) }}: ") }
                    }
                    withStyle(SpanStyle(color = (if (metric == StatsMetric.FPS) Color(0xFFB9F7CF) else Color.White.copy(alpha = 0.97f)).copy(alpha = opacity))) { append(value) }
                },
                    modifier = Modifier.testTag("stats_metric_${metric.name.lowercase(Locale.ROOT)}").fillMaxWidth()
                        .padding(top = if (firstFooter) (4 * scale).dp else 0.dp),
                    fontFamily = FontFamily.Monospace,
                    fontWeight = if (inFooter) FontWeight.Medium else FontWeight.Bold,
                    fontSize = ((if (inFooter) 8.5f else 10f) * scale).sp,
                    lineHeight = ((if (inFooter) 11f else 13f) * scale).sp,
                    maxLines = 1, overflow = TextOverflow.Ellipsis,
                    textAlign = if (right) TextAlign.End else TextAlign.Start,
                    style = detailedStyle.copy(letterSpacing = 0.sp,
                        shadow = Shadow(Color.Black, Offset.Zero, (if (inFooter) 5 else 6) * scale)))
            }
        }
        }
    }
}
