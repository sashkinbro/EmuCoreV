package com.sbro.emucorev

import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.requiredSize
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.click
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.core.NativePerformanceStats
import com.sbro.emucorev.core.PerformanceStatsSample
import com.sbro.emucorev.data.PerformanceStatsSettings
import com.sbro.emucorev.data.StatsMode
import com.sbro.emucorev.data.StatsMetric
import com.sbro.emucorev.data.StatsPosition
import com.sbro.emucorev.ui.emulation.PerformanceStatsHud
import com.sbro.emucorev.ui.theme.EmuCoreVTheme
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

class PerformanceStatsOverlayTest {
    @get:Rule val compose = createComposeRule()

    @Test fun everyMetricFitsShortLandscapeWithLargeSystemFont() {
        compose.setContent {
            EmuCoreVTheme {
                CompositionLocalProvider(LocalDensity provides Density(1f, 2f)) {
                    Box(Modifier.requiredSize(320.dp, 180.dp).testTag("stats_viewport")
                        .consumeWindowInsets(WindowInsets.safeDrawing).padding(bottom = 38.dp)) {
                        PerformanceStatsHud(PerformanceStatsSettings(mode = StatsMode.DETAILED, scalePercent = 200,
                            metricMask = StatsMetric.allMask), PerformanceStatsSample(
                            native = NativePerformanceStats(60, 17, "Adreno 740", "Vulkan", "Vita3K 0.2.0", 960, 544),
                            cpuPercent = 12.5, cpuModel = "Snapdragon 8 Gen 2", memoryMiB = 512.0))
                    }
                }
            }
        }
        val viewport = compose.onNodeWithTag("stats_viewport").fetchSemanticsNode().boundsInRoot
        StatsMetric.selectable.forEach { metric ->
            val node = compose.onNodeWithTag("stats_metric_${metric.name.lowercase(java.util.Locale.ROOT)}")
            node.assertIsDisplayed()
            val bounds = node.fetchSemanticsNode().boundsInRoot
            assertTrue("$metric must remain inside the viewport", bounds.top >= viewport.top && bounds.bottom <= viewport.bottom - 38f)
        }
    }

    @Test fun hudInBottomLeftKeepsUnderlyingGameTouchesAvailable() {
        var clicks = 0
        compose.setContent {
            EmuCoreVTheme {
                CompositionLocalProvider(LocalDensity provides Density(1f)) {
                    Box(Modifier.requiredSize(320.dp, 180.dp).testTag("stats_viewport")
                        .consumeWindowInsets(WindowInsets.safeDrawing)) {
                        Button({ clicks++ }, Modifier.fillMaxSize().testTag("stats_underlay")) { Text("Game") }
                        PerformanceStatsHud(PerformanceStatsSettings(mode = StatsMode.DETAILED,
                            position = StatsPosition.BOTTOM_LEFT, metricMask = StatsMetric.FPS.bit),
                            PerformanceStatsSample(native = NativePerformanceStats(fps = 60)))
                    }
                }
            }
        }
        val viewport = compose.onNodeWithTag("stats_viewport").fetchSemanticsNode().boundsInRoot
        val panel = compose.onNodeWithTag("performance_stats_panel").fetchSemanticsNode().boundsInRoot
        assertEquals(viewport.left + 12f, panel.left, 1f)
        assertEquals(viewport.bottom - 12f, panel.bottom, 1f)
        compose.onNodeWithTag("stats_metric_fps").performTouchInput { click(center) }
        assertEquals("The statistics HUD must not take ownership of guest touch input", 1, clicks)
    }
    @Test fun fpsModeIgnoresDetailedMetricMaskAndShowsOnlyFps() {
        compose.setContent {
            EmuCoreVTheme {
                PerformanceStatsHud(PerformanceStatsSettings(mode = StatsMode.FPS, metricMask = 0),
                    PerformanceStatsSample(native = NativePerformanceStats(fps = 60, gpu = "Adreno")))
            }
        }
        compose.onNodeWithTag("performance_stats_panel").assertIsDisplayed()
        compose.onNodeWithTag("stats_metric_fps").assertIsDisplayed()
        compose.onNodeWithTag("stats_metric_version").assertDoesNotExist()
        compose.onNodeWithTag("stats_metric_cpu").assertDoesNotExist()
        compose.onNodeWithTag("stats_metric_gpu_model").assertDoesNotExist()
    }

    @Test fun offModeDoesNotRenderEvenWithEveryMetricSelected() {
        compose.setContent {
            EmuCoreVTheme {
                PerformanceStatsHud(PerformanceStatsSettings(mode = StatsMode.OFF, metricMask = StatsMetric.allMask),
                    PerformanceStatsSample(native = NativePerformanceStats(fps = 60)))
            }
        }
        compose.onNodeWithTag("performance_stats_panel").assertDoesNotExist()
    }
}
