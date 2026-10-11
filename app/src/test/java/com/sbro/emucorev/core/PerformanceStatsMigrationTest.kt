package com.sbro.emucorev.core

import android.content.Context
import com.sbro.emucorev.data.PerformanceStatsPreferences
import com.sbro.emucorev.data.StatsPosition
import java.io.File
import org.junit.Assert.*
import org.junit.Before
import org.junit.After
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class PerformanceStatsMigrationTest {
    private lateinit var context: Context
    private lateinit var configFile: File
    private val stats get() = context.getSharedPreferences("performance_stats", Context.MODE_PRIVATE)
    @Before fun clear() {
        context = RuntimeEnvironment.getApplication()
        configFile = File(EmulatorStorage.runtimeRoot(context), "config.yml")
        configFile.delete()
        stats.edit().clear().commit()
        context.getSharedPreferences("emucorev_ui_config", Context.MODE_PRIVATE).edit().clear().commit()
    }
    @After fun cleanup() { configFile.delete() }

    @Test fun nativePreferenceMigratesOnceAndIsRemovedFromRewrittenConfig() {
        configFile.writeText("performance-overlay: true\nperformance-overlay-detail: 2\nperformance-overlay-position: 5\n")
        VitaCoreConfigRepository(context).ensureDefaultsPersisted()
        assertEquals("DETAILED", stats.getString("mode", null))
        PerformanceStatsPreferences(context).use { assertEquals(StatsPosition.BOTTOM_RIGHT, it.settings.value.position) }
        assertFalse(configFile.readText().contains("performance-overlay"))
        stats.edit().putString("mode", "OFF").commit()
        configFile.appendText("performance-overlay: true\nperformance-overlay-detail: 3\n")
        VitaCoreConfigRepository(context).load()
        assertEquals("OFF", stats.getString("mode", null))
    }

    @Test fun minimumNativeModeMapsToFps() {
        configFile.writeText("performance-overlay: true\nperformance-overlay-detail: 0\n")
        VitaCoreConfigRepository(context).load()
        assertEquals("FPS", stats.getString("mode", null))
    }

    @Test fun priorCustomHudWinsOverLegacyNativePreferences() {
        stats.edit().putBoolean("enabled", true).putInt("metrics", 0).commit()
        configFile.writeText("performance-overlay: false\n")
        VitaCoreConfigRepository(context).load()
        assertEquals("DETAILED", stats.getString("mode", null))
        PerformanceStatsPreferences(context).use { assertEquals(0, it.settings.value.metricMask) }
    }
}
