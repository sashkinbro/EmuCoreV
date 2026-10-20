package com.sbro.emucorev.ui.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.background
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.Alignment
import androidx.compose.material3.Switch
import com.sbro.emucorev.ui.theme.neon.neonShape
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.foundation.layout.RowScope
import com.sbro.emucorev.ui.gamemanager.ChoiceContainer as GameManagerChoiceContainer
import com.sbro.emucorev.ui.gamemanager.ToggleRow as GameManagerToggleRow
import com.sbro.emucorev.ui.gamemanager.SliderRow as GameManagerSliderRow
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.ui.theme.neon.neonChipShape
import com.sbro.emucorev.R
import com.sbro.emucorev.data.PerformanceStatsPreferences
import com.sbro.emucorev.data.PerformanceStatsSettings
import com.sbro.emucorev.data.StatsMode
import androidx.compose.ui.platform.testTag
import com.sbro.emucorev.data.StatsMetric
import com.sbro.emucorev.data.StatsPosition
import com.sbro.emucorev.ui.common.horizontalBleed
import com.sbro.emucorev.ui.theme.CardContentPadding
import androidx.compose.ui.unit.Dp
import com.sbro.emucorev.ui.common.SectionCard
import com.sbro.emucorev.ui.emulation.statsMetricLabel
import kotlin.math.roundToInt

@Composable
fun PerformanceStatsSettingsSection(
    compact: Boolean = false,
    showTitle: Boolean = true,
    gameManagerStyle: Boolean = false,
    extraRowPadding: Dp = SettingsRowHorizontalPadding,
    scrollBleed: Dp = if (compact) 14.dp else CardContentPadding,
    textPrimary: Color = MaterialTheme.colorScheme.onSurface,
    textSecondary: Color = MaterialTheme.colorScheme.onSurfaceVariant,
    rowBackground: Color = Color.Transparent,
    titleId: String? = null
) {
    val context = LocalContext.current
    val preferences = remember(context, titleId) { PerformanceStatsPreferences(context, titleId) }
    val settings by preferences.settings.collectAsState()
    DisposableEffect(preferences) { onDispose { preferences.close() } }
    if (!compact) {
        SectionCard(stringResource(R.string.stats_title)) {
            StatsScreenControls(settings, preferences, gameManagerStyle)
        }
        return
    }
    val labelPadding = if (compact) 0.dp else extraRowPadding
    val content: @Composable () -> Unit = {
        Row(Modifier.fillMaxWidth().clip(neonShape(12.dp)).background(rowBackground)
            .padding(horizontal = if (compact) 12.dp else labelPadding, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.stats_enabled), Modifier.weight(1f),
                style = MaterialTheme.typography.bodyMedium, color = textPrimary)
            Switch(settings.enabled, preferences::setEnabled, Modifier.testTag("stats_enabled"))
        }
        Text(stringResource(R.string.stats_mode), Modifier.padding(horizontal = labelPadding), style = MaterialTheme.typography.labelLarge, color = textSecondary)
        StatsChipRow(scrollBleed, labelPadding) {
            listOf(StatsMode.FPS, StatsMode.DETAILED).forEach { mode ->
                FilterChip(shape = neonChipShape(), selected = settings.rememberedMode == mode,
                    onClick = { preferences.selectMode(mode) },
                    modifier = Modifier.testTag("stats_mode_${mode.name.lowercase(java.util.Locale.ROOT)}"),
                    label = { Text(stringResource(if (mode == StatsMode.FPS) R.string.stats_mode_fps else R.string.stats_mode_detailed)) })
            }
        }
        run {
            Text(stringResource(R.string.stats_position), Modifier.padding(horizontal = labelPadding), style = MaterialTheme.typography.labelLarge, color = textSecondary)
            StatsChipRow(scrollBleed, labelPadding) {
                StatsPosition.entries.forEach { position ->
                    FilterChip(shape = neonChipShape(), selected = settings.position == position, onClick = { preferences.update { it.copy(position = position) } },
                        label = { Text(stringResource(when (position) {
                            StatsPosition.TOP_LEFT -> R.string.stats_top_left
                            StatsPosition.TOP_RIGHT -> R.string.stats_top_right
                            StatsPosition.BOTTOM_LEFT -> R.string.stats_bottom_left
                            StatsPosition.BOTTOM_RIGHT -> R.string.stats_bottom_right
                        })) })
                }
            }
            StatsSlider(stringResource(R.string.stats_size), settings.scalePercent, 75f..200f,
                textPrimary, textSecondary, rowBackground, compact, labelPadding) { value -> preferences.update { it.copy(scalePercent = value) } }
            if (settings.rememberedMode == StatsMode.DETAILED) {
                Text(stringResource(R.string.stats_metrics), Modifier.padding(horizontal = labelPadding), style = MaterialTheme.typography.labelLarge, color = textSecondary)
                StatsChipRow(scrollBleed, labelPadding) {
                    StatsMetric.selectable.forEach { metric ->
                        FilterChip(shape = neonChipShape(), selected = settings.shows(metric),
                            onClick = { preferences.update { it.copy(metricMask = it.metricMask xor metric.selectionMask) } },
                            label = { Text(statsMetricLabel(metric)) })
                    }
                }
            }
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = neonShape(12.dp),
                color = rowBackground,
                border = BorderStroke(1.dp, textSecondary.copy(alpha = 0.18f))
            ) {
                Row(
                    modifier = Modifier.clickable { preferences.reset() }
                        .padding(horizontal = 12.dp, vertical = 14.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(stringResource(R.string.stats_reset),
                        style = MaterialTheme.typography.bodyMedium, color = textPrimary)
                }
            }
        }
    }
    if (compact) {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            if (showTitle) Text(stringResource(R.string.stats_title), style = MaterialTheme.typography.titleSmall, color = textPrimary)
            content()
        }
    } else {
        SectionCard(stringResource(R.string.stats_title), content = content)
    }
}

@Composable
private fun StatsChipRow(bleed: Dp, extraRowPadding: Dp, content: @Composable () -> Unit) {
    Row(Modifier.fillMaxWidth().horizontalBleed(bleed).horizontalScroll(rememberScrollState())
        .padding(horizontal = bleed + extraRowPadding),
        horizontalArrangement = Arrangement.spacedBy(8.dp)) { content() }
}

@Composable
private fun StatsSlider(label: String, value: Int, range: ClosedFloatingPointRange<Float>,
    textPrimary: Color, textSecondary: Color, rowBackground: Color, compact: Boolean, extraRowPadding: Dp, change: (Int) -> Unit) {
    Column(Modifier.fillMaxWidth().clip(neonShape(12.dp)).background(rowBackground)
        .padding(horizontal = if (compact) 12.dp else extraRowPadding, vertical = 9.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            Text(label, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium, color = textPrimary)
            Text("$value%", style = MaterialTheme.typography.labelMedium, color = textSecondary)
        }
        Slider(value.toFloat(), { change(it.roundToInt()) }, valueRange = range)
    }
}


@Composable
private fun StatsScreenControls(
    settings: PerformanceStatsSettings,
    preferences: PerformanceStatsPreferences,
    gameManagerStyle: Boolean
) {
    val defaults = remember { PerformanceStatsSettings() }
    val help = stringResource(R.string.stats_help)
    val enabledTitle = stringResource(R.string.stats_enabled)
    if (gameManagerStyle) {
        GameManagerToggleRow(enabledTitle, settings.enabled, help,
            onReset = { preferences.setEnabled(defaults.enabled) }, onCheckedChange = preferences::setEnabled)
    } else {
        SettingToggleRow(enabledTitle, help, settings.enabled, preferences::setEnabled,
            onResetDefault = { preferences.setEnabled(defaults.enabled) })
    }
    StatsScreenChoiceRow(stringResource(R.string.stats_mode), help, gameManagerStyle,
        onReset = { preferences.selectMode(defaults.rememberedMode) }) {
        listOf(StatsMode.FPS, StatsMode.DETAILED).forEach { mode ->
            StatsScreenChip(settings.rememberedMode == mode, gameManagerStyle,
                Modifier.testTag("stats_mode_${mode.name.lowercase(java.util.Locale.ROOT)}"),
                { preferences.selectMode(mode) }) {
                Text(stringResource(if (mode == StatsMode.FPS) R.string.stats_mode_fps else R.string.stats_mode_detailed))
            }
        }
    }
    val positionTitle = stringResource(R.string.stats_position)
    StatsScreenChoiceRow(positionTitle, positionTitle, gameManagerStyle,
        onReset = { preferences.update { it.copy(position = defaults.position) } }) {
        StatsPosition.entries.forEach { position ->
            StatsScreenChip(settings.position == position, gameManagerStyle,
                onClick = { preferences.update { it.copy(position = position) } }) {
                Text(stringResource(when (position) {
                    StatsPosition.TOP_LEFT -> R.string.stats_top_left
                    StatsPosition.TOP_RIGHT -> R.string.stats_top_right
                    StatsPosition.BOTTOM_LEFT -> R.string.stats_bottom_left
                    StatsPosition.BOTTOM_RIGHT -> R.string.stats_bottom_right
                }))
            }
        }
    }
    val sizeTitle = stringResource(R.string.stats_size)
    val resetSize = { preferences.update { it.copy(scalePercent = defaults.scalePercent) } }
    val changeSize: (Float) -> Unit = { value -> preferences.update { it.copy(scalePercent = value.roundToInt()) } }
    if (gameManagerStyle) {
        GameManagerSliderRow(sizeTitle, sizeTitle, "${settings.scalePercent}%", settings.scalePercent.toFloat(),
            75f..200f, onReset = resetSize, onChange = changeSize)
    } else {
        SettingSliderRow(sizeTitle, sizeTitle, "${settings.scalePercent}%", resetSize) {
            Slider(settings.scalePercent.toFloat(), changeSize, valueRange = 75f..200f)
        }
    }
    if (settings.rememberedMode == StatsMode.DETAILED) {
        val metricsTitle = stringResource(R.string.stats_metrics)
        StatsScreenChoiceRow(metricsTitle, metricsTitle, gameManagerStyle,
            onReset = { preferences.update { it.copy(metricMask = defaults.metricMask) } }) {
            StatsMetric.selectable.forEach { metric ->
                StatsScreenChip(settings.shows(metric), gameManagerStyle,
                    onClick = { preferences.update { it.copy(metricMask = it.metricMask xor metric.selectionMask) } }) {
                    Text(statsMetricLabel(metric))
                }
            }
        }
    }
    TextButton({ preferences.reset() }) { Text(stringResource(R.string.stats_reset)) }
}

@Composable
private fun StatsScreenChoiceRow(title: String, description: String, gameManagerStyle: Boolean,
    onReset: () -> Unit, content: @Composable RowScope.() -> Unit) {
    if (gameManagerStyle) GameManagerChoiceContainer(title, description, onReset, content)
    else SettingChoiceRow(title, description, onReset, content)
}

@Composable
private fun StatsScreenChip(selected: Boolean, gameManagerStyle: Boolean,
    modifier: Modifier = Modifier, onClick: () -> Unit, label: @Composable () -> Unit) {
    FilterChip(selected = selected, onClick = onClick, modifier = modifier, shape = neonChipShape(),
        colors = if (gameManagerStyle) FilterChipDefaults.filterChipColors(
            selectedContainerColor = MaterialTheme.colorScheme.secondaryContainer,
            selectedLabelColor = MaterialTheme.colorScheme.onSecondaryContainer
        ) else FilterChipDefaults.filterChipColors(), label = label)
}
