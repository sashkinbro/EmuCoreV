package com.sbro.emucorev.data

import android.content.Context
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class PerformanceStatsPreferencesTest {
    private lateinit var context: Context

    @Before fun clear() {
        context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("performance_stats", Context.MODE_PRIVATE).edit().clear().commit()
    }

    @Test fun settingsRoundTripAndKeepIndependentMetricChoices() {
        PerformanceStatsPreferences(context).use { repository ->
            repository.update { it.copy(mode = StatsMode.DETAILED, position = StatsPosition.BOTTOM_LEFT,
                scalePercent = 175,
                metricMask = StatsMetric.FPS.bit or StatsMetric.GPU_USAGE.bit) }
        }
        PerformanceStatsPreferences(context).use { repository ->
            val saved = repository.settings.value
            assertTrue(saved.enabled)
            assertEquals(StatsPosition.BOTTOM_LEFT, saved.position)
            assertEquals(175, saved.scalePercent)
            assertTrue(saved.shows(StatsMetric.FPS))
            assertTrue(saved.shows(StatsMetric.GPU_USAGE))
            assertFalse(saved.shows(StatsMetric.CPU))
        }
    }

    @Test fun malformedPreferencesAreBoundedWithoutEnablingUnknownMetrics() {
        context.getSharedPreferences("performance_stats", Context.MODE_PRIVATE).edit()
            .putInt("position", 999).putInt("scale", Int.MAX_VALUE)
            .putInt("text_opacity", -100).putInt("background_opacity", 200)
            .putInt("metrics", -1).commit()
        PerformanceStatsPreferences(context).use { repository ->
            val saved = repository.settings.value
            assertEquals(StatsPosition.TOP_RIGHT, saved.position)
            assertEquals(200, saved.scalePercent)
            assertEquals(StatsMetric.allMask, saved.metricMask)
        }
    }

    @Test fun separateRepositoryEditsDoNotOverwritePreviouslyChangedFields() {
        PerformanceStatsPreferences(context).use { first ->
            PerformanceStatsPreferences(context).use { second ->
                first.update { it.copy(mode = StatsMode.DETAILED) }
                second.update { it.copy(scalePercent = 125) }
                assertTrue(second.settings.value.enabled)
                assertEquals(125, second.settings.value.scalePercent)
                first.update { it.copy(metricMask = 0) }
                assertEquals(125, first.settings.value.scalePercent)
                assertEquals(0, first.settings.value.metricMask)
            }
        }
    }

    @Test fun modesRoundTripWithoutLosingDetailedMetricSelection() {
        val mask = StatsMetric.CPU.bit or StatsMetric.GPU_MODEL.bit
        PerformanceStatsPreferences(context).use { repository ->
            assertEquals(StatsMode.OFF, repository.settings.value.mode)
            repository.update { it.copy(mode = StatsMode.DETAILED, metricMask = mask) }
            repository.update { it.copy(mode = StatsMode.FPS) }
            assertEquals(StatsMetric.CPU.selectionMask or StatsMetric.GPU_MODEL.selectionMask, repository.settings.value.metricMask)
        }
        PerformanceStatsPreferences(context).use { repository ->
            assertEquals(StatsMode.FPS, repository.settings.value.mode)
            repository.update { it.copy(mode = StatsMode.OFF) }
            assertFalse(repository.settings.value.enabled)
            repository.update { it.copy(mode = StatsMode.DETAILED) }
            assertEquals(StatsMetric.CPU.selectionMask or StatsMetric.GPU_MODEL.selectionMask, repository.settings.value.metricMask)
        }
    }
    @Test fun switchRemembersStyleAndFreshDefaultsMatchReference() {
        PerformanceStatsPreferences(context).use { repository ->
            val fresh = repository.settings.value
            assertFalse(fresh.enabled)
            assertEquals(StatsMode.DETAILED, fresh.rememberedMode)
            assertEquals(StatsPosition.TOP_RIGHT, fresh.position)
            assertEquals(100, fresh.scalePercent)
            val expected = StatsMetric.allMask xor StatsMetric.TEMPERATURE.bit xor StatsMetric.THERMAL.bit
            assertEquals(expected, fresh.metricMask)
            repository.selectMode(StatsMode.FPS)
            assertFalse(repository.settings.value.enabled)
            repository.setEnabled(true)
            assertEquals(StatsMode.FPS, repository.settings.value.mode)
            repository.setEnabled(false)
        }
        PerformanceStatsPreferences(context).use { repository ->
            assertFalse(repository.settings.value.enabled)
            assertEquals(StatsMode.FPS, repository.settings.value.rememberedMode)
            repository.setEnabled(true)
            assertEquals(StatsMode.FPS, repository.settings.value.mode)
            repository.selectMode(StatsMode.DETAILED)
            repository.setEnabled(false)
            repository.setEnabled(true)
            assertEquals(StatsMode.DETAILED, repository.settings.value.mode)
        }
    }
    @Test fun prototypeDefaultMaskMigratesOnceAndCustomMasksRemainSelected() {
        val stored = context.getSharedPreferences("performance_stats", Context.MODE_PRIVATE)
        for (mask in listOf(175, 0, StatsMetric.FPS.bit)) {
            stored.edit().clear().putString("mode", "DETAILED").putInt("metrics", mask)
                .putInt("text_opacity", 100).putInt("background_opacity", 0).commit()
            PerformanceStatsPreferences(context).use { repository ->
                assertEquals(if (mask == 175) 1279 else mask, repository.settings.value.metricMask)
                assertFalse(stored.contains("text_opacity"))
                assertFalse(stored.contains("background_opacity"))
                assertEquals(StatsMode.DETAILED, repository.settings.value.mode)
                repository.update { it.copy(metricMask = 175) }
            }
            PerformanceStatsPreferences(context).use { repository ->
                assertEquals(PerformanceStatsSettings(metricMask = 175).normalized().metricMask,
                    repository.settings.value.metricMask)
            }
        }
    }

    @Test fun unmodifiedGamesInheritGlobalChangesLive() {
        PerformanceStatsPreferences(context).use { global ->
            PerformanceStatsPreferences(context, "PCSE00333").use { resident ->
                PerformanceStatsPreferences(context, "PCSE00001").use { ridge ->
                    global.update { it.copy(mode = StatsMode.DETAILED, scalePercent = 150) }
                    assertEquals(global.settings.value, resident.settings.value)
                    assertEquals(global.settings.value, ridge.settings.value)
                    global.update { it.copy(position = StatsPosition.BOTTOM_LEFT, metricMask = StatsMetric.FPS.bit) }
                    assertEquals(global.settings.value, resident.settings.value)
                    assertEquals(global.settings.value, ridge.settings.value)
                }
            }
        }
    }

    @Test fun firstGameEditFreezesEffectiveSnapshotWithoutChangingOtherGames() {
        PerformanceStatsPreferences(context).use { global ->
            global.update { it.copy(mode = StatsMode.DETAILED, position = StatsPosition.BOTTOM_LEFT,
                scalePercent = 150, metricMask = StatsMetric.CPU.selectionMask) }
            PerformanceStatsPreferences(context, "PCSE00333").use { resident ->
                PerformanceStatsPreferences(context, "PCSE00001").use { ridge ->
                    resident.update { it.copy(scalePercent = 125) }
                    val expected = global.settings.value.copy(scalePercent = 125)
                    assertEquals(expected, resident.settings.value)
                    assertEquals(global.settings.value, ridge.settings.value)
                    global.update { it.copy(mode = StatsMode.FPS, position = StatsPosition.TOP_RIGHT, metricMask = 0) }
                    assertEquals(expected, resident.settings.value)
                    assertEquals(global.settings.value, ridge.settings.value)
                    ridge.setEnabled(false)
                    assertTrue(resident.settings.value.enabled)
                    assertTrue(global.settings.value.enabled)
                }
            }
        }
    }

    @Test fun gameResetRemovesOverrideAndReturnsToLiveInheritanceAfterReload() {
        PerformanceStatsPreferences(context).use { global ->
            global.update { it.copy(mode = StatsMode.FPS, scalePercent = 150) }
            PerformanceStatsPreferences(context, "PCSE00333").use { game ->
                game.update { it.copy(position = StatsPosition.BOTTOM_RIGHT, scalePercent = 75) }
            }
            PerformanceStatsPreferences(context, "PCSE00333").use { game ->
                assertEquals(75, game.settings.value.scalePercent)
                game.reset()
                assertEquals(global.settings.value, game.settings.value)
                global.update { it.copy(scalePercent = 175) }
                assertEquals(global.settings.value, game.settings.value)
            }
            PerformanceStatsPreferences(context, "PCSE00333").use { game ->
                assertEquals(global.settings.value, game.settings.value)
            }
        }
    }

    @Test fun twoGameRepositoriesMergeEditsAndGlobalResetKeepsGameOverride() {
        PerformanceStatsPreferences(context).use { global ->
            global.setEnabled(true)
            PerformanceStatsPreferences(context, "PCSE00333").use { first ->
                PerformanceStatsPreferences(context, "PCSE00333").use { second ->
                    first.update { it.copy(scalePercent = 150) }
                    second.update { it.copy(position = StatsPosition.BOTTOM_LEFT) }
                    assertEquals(150, first.settings.value.scalePercent)
                    assertEquals(StatsPosition.BOTTOM_LEFT, first.settings.value.position)
                    global.reset()
                    assertEquals(PerformanceStatsSettings(), global.settings.value)
                    assertTrue(first.settings.value.enabled)
                    assertEquals(second.settings.value, first.settings.value)
                }
            }
            PerformanceStatsPreferences(context, "PCSE00333").use { reopened ->
                assertEquals(150, reopened.settings.value.scalePercent)
                assertEquals(StatsPosition.BOTTOM_LEFT, reopened.settings.value.position)
                assertTrue(reopened.settings.value.enabled)
            }
        }
    }

    @Test fun gameFirstOpenRunsLegacyGlobalMigrationAndCannotMigrateIntoGame() {
        context.getSharedPreferences("performance_stats", Context.MODE_PRIVATE).edit()
            .putBoolean("enabled", true).putInt("metrics", 175).putInt("text_opacity", 100).commit()
        PerformanceStatsPreferences(context, "PCSE00333").use { game ->
            assertEquals(StatsMode.DETAILED, game.settings.value.mode)
            assertEquals(1279, game.settings.value.metricMask)
            game.setEnabled(false)
            game.migrateLegacyCore(mapOf("performance-overlay" to "true"))
            assertEquals(StatsMode.OFF, game.settings.value.mode)
        }
        PerformanceStatsPreferences(context).use { global ->
            assertEquals(StatsMode.DETAILED, global.settings.value.mode)
            assertEquals(1279, global.settings.value.metricMask)
        }
    }

    @Test fun blankTitleUsesGlobalScopeAndTitleKeysCannotAlias() {
        PerformanceStatsPreferences(context, " ").use { global ->
            global.update { it.copy(scalePercent = 150) }
        }
        PerformanceStatsPreferences(context).use { global ->
            assertEquals(150, global.settings.value.scalePercent)
        }
        PerformanceStatsPreferences(context, "A").use { first ->
            first.update { it.copy(scalePercent = 75) }
            PerformanceStatsPreferences(context, "A:mode").use { other ->
                assertEquals(150, other.settings.value.scalePercent)
                other.update { it.copy(scalePercent = 175) }
                assertEquals(75, first.settings.value.scalePercent)
            }
        }
    }
}
