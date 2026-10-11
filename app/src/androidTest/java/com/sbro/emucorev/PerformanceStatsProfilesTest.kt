package com.sbro.emucorev

import android.content.Context
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.unit.dp
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorev.core.NativePerformanceStats
import com.sbro.emucorev.core.PerformanceStatsSample
import com.sbro.emucorev.data.PerformanceStatsPreferences
import com.sbro.emucorev.data.StatsMode
import com.sbro.emucorev.data.StatsMetric
import com.sbro.emucorev.ui.emulation.PerformanceStatsHud
import com.sbro.emucorev.ui.settings.PerformanceStatsSettingsSection
import com.sbro.emucorev.ui.theme.EmuCoreVTheme
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class PerformanceStatsProfilesTest {
    @get:Rule val compose = createComposeRule()

    @Test fun actualSectionKeepsGameEditsSeparateAndBlankTitleEditsGlobalDefaults() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val stored = context.getSharedPreferences("performance_stats", Context.MODE_PRIVATE)
        val saved = stored.all.toMap()
        val gameA = "__stats_profile_test_A__"
        val gameB = "__stats_profile_test_B__"
        val title = mutableStateOf<String?>(gameA)
        try {
            PerformanceStatsPreferences(context).use { global ->
                PerformanceStatsPreferences(context, gameA).use { first ->
                    PerformanceStatsPreferences(context, gameB).use { second ->
                        first.reset()
                        second.reset()
                        global.update { it.copy(mode = StatsMode.DETAILED,
                            metricMask = StatsMetric.VERSION.bit or StatsMetric.FPS.bit) }
                        compose.setContent {
                            EmuCoreVTheme {
                                val currentContext = LocalContext.current
                                val preferences = remember(currentContext, title.value) {
                                    PerformanceStatsPreferences(currentContext, title.value)
                                }
                                DisposableEffect(preferences) { onDispose { preferences.close() } }
                                val effective by preferences.settings.collectAsState()
                                Column {
                                    Box(Modifier.fillMaxWidth().height(100.dp)) {
                                        PerformanceStatsHud(effective, PerformanceStatsSample(
                                            native = NativePerformanceStats(fps = 60, core = "Vita3K")))
                                    }
                                    PerformanceStatsSettingsSection(compact = true, titleId = title.value)
                                }
                            }
                        }
                        compose.onNodeWithTag("stats_metric_version").assertIsDisplayed()
                        compose.onNodeWithTag("stats_mode_fps").performClick()
                        compose.onNodeWithTag("stats_metric_version").assertDoesNotExist()
                        compose.onNodeWithTag("stats_metric_fps").assertIsDisplayed()
                        compose.runOnIdle {
                            assertEquals(StatsMode.FPS, first.settings.value.mode)
                            assertEquals(StatsMode.DETAILED, second.settings.value.mode)
                            assertEquals(StatsMode.DETAILED, global.settings.value.mode)
                            title.value = gameB
                        }
                        compose.onNodeWithTag("stats_mode_detailed").assertIsSelected()
                        compose.onNodeWithTag("stats_metric_version").assertIsDisplayed()
                        compose.runOnIdle { title.value = " " }
                        compose.onNodeWithTag("stats_mode_fps").performClick()
                        compose.runOnIdle {
                            assertEquals(StatsMode.FPS, global.settings.value.mode)
                            assertEquals(StatsMode.FPS, second.settings.value.mode)
                            assertEquals(StatsMode.FPS, first.settings.value.mode)
                            global.selectMode(StatsMode.DETAILED)
                            assertEquals(StatsMode.FPS, first.settings.value.mode)
                            assertEquals(StatsMode.DETAILED, second.settings.value.mode)
                            title.value = gameB
                        }
                        compose.onNodeWithTag("stats_metric_version").assertIsDisplayed()
                    }
                }
            }
        } finally {
            // Preserve the actual user's preferences even if a UI assertion fails.
            val edit = stored.edit().clear()
            saved.forEach { (key, value) ->
                when (value) {
                    is String -> edit.putString(key, value)
                    is Int -> edit.putInt(key, value)
                    is Long -> edit.putLong(key, value)
                    is Float -> edit.putFloat(key, value)
                    is Boolean -> edit.putBoolean(key, value)
                    is Set<*> -> edit.putStringSet(key, value.filterIsInstance<String>().toSet())
                }
            }
            edit.commit()
        }
    }
}
