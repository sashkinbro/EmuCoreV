package com.sbro.emucorev.core

import android.content.Context
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.CustomizationPreferences
import com.sbro.emucorev.data.PerformanceStatsPreferences
import com.sbro.emucorev.data.PerformanceStatsSettings
import com.sbro.emucorev.data.StatsMode
import com.sbro.emucorev.data.StatsMetric
import com.sbro.emucorev.data.StatsPosition
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.After
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class SettingsBackupPerformanceStatsTest {
    private lateinit var context: Context
    private lateinit var customization: CustomizationPreferences

    @Before fun clearStats() {
        context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("performance_stats", Context.MODE_PRIVATE).edit().clear().commit()
        customization = CustomizationPreferences(context)
    }

    @After fun closePreferences() { customization.close() }

    private fun backup() = SettingsBackupRepository(context, AppPreferences(context),
        VitaCoreConfigRepository(context), customization)

    @Test fun exportedBackupRestoresEveryStatsChoiceWithoutRetiredNativeFields() {
        val expected = PerformanceStatsSettings(mode = StatsMode.DETAILED, position = StatsPosition.BOTTOM_LEFT, scalePercent = 175,
            metricMask = StatsMetric.CPU.selectionMask or StatsMetric.GPU_MODEL.selectionMask or StatsMetric.THERMAL.bit)
        val core = VitaCoreConfigRepository(context)
        core.ensureDefaultsPersisted()
        PerformanceStatsPreferences(context).use { stats ->
            stats.update { expected }
            val exported = JSONObject(backup().exportJson().toString())
            assertEquals(1, exported.getInt("format"))
            assertEquals("BOTTOM_LEFT", exported.getJSONObject("performanceStats").getString("position"))
            stats.update { PerformanceStatsSettings() }
            backup().restoreJson(exported, applyApp = false)
            assertEquals(expected, stats.settings.value)
            assertEquals(false, exported.getJSONObject("core").has("performanceOverlay"))
            assertEquals("DETAILED", exported.getJSONObject("performanceStats").getString("mode"))
        }
    }

    @Test fun legacyBackupWithoutStatsLeavesExistingStatsUntouched() {
        PerformanceStatsPreferences(context).use { stats ->
            stats.update { it.copy(mode = StatsMode.DETAILED, metricMask = 0, scalePercent = 125) }
            val expected = stats.settings.value
            backup().restoreJson(JSONObject().put("format", 1), applyApp = false, applyCore = false)
            assertEquals(expected, stats.settings.value)
        }
    }

    @Test fun excludingCustomizationDoesNotRestoreStats() {
        val root = JSONObject().put("performanceStats", JSONObject().put("enabled", true)
            .put("position", "BOTTOM_RIGHT").put("scalePercent", 200))
        PerformanceStatsPreferences(context).use { stats ->
            val expected = stats.settings.value
            backup().restoreJson(root, applyApp = false, applyCore = false, applyCustomization = false)
            assertEquals(expected, stats.settings.value)
        }
    }

    @Test fun malformedValuesKeepTypedChoicesAndClampSupportedRanges() {
        val root = JSONObject().put("performanceStats", JSONObject().put("enabled", "true")
            .put("position", "UNKNOWN").put("scalePercent", Int.MAX_VALUE)
            .put("textOpacity", -100).put("backgroundOpacity", 1000).put("metricMask", -1))
        PerformanceStatsPreferences(context).use { stats ->
            stats.update { it.copy(position = StatsPosition.TOP_LEFT) }
            backup().restoreJson(root, applyApp = false, applyCore = false)
            assertEquals(PerformanceStatsSettings(mode = StatsMode.OFF, position = StatsPosition.TOP_LEFT, scalePercent = 200,
                metricMask = StatsMetric.allMask), stats.settings.value)
        }
    }

    @Test fun overflowingOrFractionalIntegersDoNotWrapPreferences() {
        val root = JSONObject().put("performanceStats", JSONObject().put("scalePercent", Long.MAX_VALUE)
            .put("textOpacity", 42.5).put("metricMask", "2047"))
        PerformanceStatsPreferences(context).use { stats ->
            val expected = stats.settings.value
            backup().restoreJson(root, applyApp = false, applyCore = false)
            assertEquals(expected, stats.settings.value)
        }
    }
    @Test fun modeBackupsAndEarlierBooleanDomainRestoreCorrectly() {
        PerformanceStatsPreferences(context).use { stats ->
            backup().restoreJson(JSONObject().put("performanceStats", JSONObject().put("enabled", true)),
                applyApp = false, applyCore = false)
            assertEquals(StatsMode.DETAILED, stats.settings.value.mode)
            backup().restoreJson(JSONObject().put("performanceStats", JSONObject().put("mode", "FPS").put("enabled", false)),
                applyApp = false, applyCore = false)
            assertEquals(StatsMode.FPS, stats.settings.value.mode)
            backup().restoreJson(JSONObject().put("performanceStats", JSONObject().put("mode", "INVALID").put("enabled", false)),
                applyApp = false, applyCore = false)
            assertEquals(StatsMode.FPS, stats.settings.value.mode)
            backup().restoreJson(JSONObject().put("performanceStats", JSONObject().put("enabled", false)),
                applyApp = false, applyCore = false)
            assertEquals(StatsMode.OFF, stats.settings.value.mode)
        }
    }

    @Test fun oldNativeBackupMigratesWithoutRecreatingOldConfigFields() {
        PerformanceStatsPreferences(context).use { stats ->
            val legacy = JSONObject().put("core", JSONObject().put("performanceOverlay", true)
                .put("performanceOverlayDetail", 0).put("performanceOverlayPosition", 3))
            backup().restoreJson(legacy, applyApp = false)
            assertEquals(StatsMode.FPS, stats.settings.value.mode)
            assertEquals(StatsPosition.BOTTOM_LEFT, stats.settings.value.position)
            val exported = backup().exportJson()
            assertEquals(false, exported.getJSONObject("core").has("performanceOverlay"))
            assertEquals("FPS", exported.getJSONObject("performanceStats").getString("mode"))
        }
    }
    @Test fun disabledBackupPreservesRememberedStyle() {
        PerformanceStatsPreferences(context).use { stats ->
            stats.selectMode(StatsMode.FPS)
            val exported = backup().exportJson()
            assertEquals("OFF", exported.getJSONObject("performanceStats").getString("mode"))
            assertEquals("FPS", exported.getJSONObject("performanceStats").getString("rememberedMode"))
            stats.selectMode(StatsMode.DETAILED)
            backup().restoreJson(exported, applyApp = false, applyCore = false)
            assertEquals(StatsMode.FPS, stats.settings.value.rememberedMode)
            stats.setEnabled(true)
            assertEquals(StatsMode.FPS, stats.settings.value.mode)
        }
    }
    @Test fun globalBackupRestorePreservesGameOverrideAndUpdatesInheritingGames() {
        PerformanceStatsPreferences(context).use { global ->
            PerformanceStatsPreferences(context, "PCSE00608").use { resident ->
                PerformanceStatsPreferences(context, "PCSE00001").use { ridge ->
                    global.update { it.copy(mode = StatsMode.FPS, scalePercent = 150) }
                    val exported = backup().exportJson()
                    resident.update { it.copy(mode = StatsMode.DETAILED, scalePercent = 75) }
                    val gameOverride = resident.settings.value
                    global.update { PerformanceStatsSettings() }
                    backup().restoreJson(exported, applyApp = false, applyCore = false)
                    assertEquals(StatsMode.FPS, global.settings.value.mode)
                    assertEquals(150, global.settings.value.scalePercent)
                    assertEquals(global.settings.value, ridge.settings.value)
                    assertEquals(gameOverride, resident.settings.value)
                }
            }
        }
    }
}
