package com.sbro.emucorev.core

import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.readText
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SettingsPersistenceContractTest {
    @Test
    fun everyGlobalCoreSettingHasSymmetricLoadSaveAndPreferenceCoverage() {
        val source = javaSource("core/VitaCoreConfigRepository.kt")
        val fields = Regex("""val\s+(\w+)\s*:""")
            .findAll(source.substringAfter("data class VitaCoreConfig(").substringBefore(") {"))
            .map { it.groupValues[1] }
            .toSet()
        val loadBlock = source.substringAfter("return normalizeForBuild(")
            .substringBefore("fun ensureDefaultsPersisted")
        val loadedFields = Regex("""(?m)^\s*(\w+)\s*=""")
            .findAll(loadBlock)
            .map { it.groupValues[1] }
            .toSet()
        val saveBlock = source.substringAfter("fun save(inputConfig")
            .substringBefore("fun resetToDefaults")
        val savedFields = Regex("""config\.(\w+)""")
            .findAll(saveBlock)
            .map { it.groupValues[1] }
            .toSet()
        val savedKeys = Regex("""values\["([^"]+)"\]\s*=""")
            .findAll(saveBlock)
            .map { it.groupValues[1] }
            .toSet()
        val persistedKeys = Regex(""""([^"]+)"""")
            .findAll(
                source.substringAfter("private val persistedKeys = setOf(")
                    .substringBefore("\n    )")
            )
            .map { it.groupValues[1] }
            .toSet()

        assertEquals("Every VitaCoreConfig field must be restored", fields, loadedFields.intersect(fields))
        assertEquals(
            "Every user-controlled field must be written; two upstream UI flags are intentionally fixed off",
            fields - setOf("showLiveAreaScreen", "checkForUpdates"),
            savedFields.intersect(fields)
        )
        assertEquals("The preference mirror must cover every serialized key", savedKeys, persistedKeys)
    }

    @Test
    fun schemaMigrationSurvivesNativeRewritesWithoutResettingUserChoices() {
        val source = javaSource("core/VitaCoreConfigRepository.kt")
        val migration = source.substringAfter("private fun applyMigrations")
            .substringBefore("fun save(inputConfig")

        assertTrue(
            "Schema version must fall back to the SharedPreferences mirror",
            "prefs.getString(SCHEMA_VERSION_KEY, null)" in source
        )
        assertFalse(
            "A schema upgrade must never reset the shader notice preference",
            "showCompileShaders =" in migration
        )
    }

    @Test
    fun settingsBackupRoundTripsEveryCoreField() {
        val core = javaSource("core/VitaCoreConfigRepository.kt")
        val backup = javaSource("core/SettingsBackupRepository.kt")
        val fields = Regex("""val\s+(\w+)\s*:""")
            .findAll(core.substringAfter("data class VitaCoreConfig(").substringBefore(") {"))
            .map { it.groupValues[1] }
            .toSet()
        val exportBlock = backup.substringAfter("private fun VitaCoreConfig.toJson()")
            .substringBefore("private fun JSONObject.toVitaCoreConfig")
        val importBlock = backup.substringAfter("private fun JSONObject.toVitaCoreConfig")
            .substringBefore("private fun JSONObject.optFloat")

        fields.forEach { field ->
            assertTrue("Backup export is missing $field", Regex("""\.put\("$field",\s*[^\n]*\b$field\b""").containsMatchIn(exportBlock))
            assertTrue("Backup restore is missing $field", Regex("""(?m)^\s*$field\s*=""").containsMatchIn(importBlock))
        }
    }

    @Test
    fun everyPerGameUiSettingIsReadAndWrittenByTheProfileRepository() {
        val screen = javaSource("ui/gamemanager/GameManagerScreen.kt")
        val repository = javaSource("core/VitaGameSettingsRepository.kt")
        val fields = Regex("""config\.(\w+)""")
            .findAll(screen)
            .map { it.groupValues[1] }
            .toSet()

        fields.forEach { field ->
            val isRead = Regex("""(?m)^\s*$field\s*=""").containsMatchIn(repository) ||
                Regex("""config\.copy\([^)]*\b$field\s*=""").containsMatchIn(repository)
            assertTrue("Per-game profile does not restore $field", isRead)
            assertTrue("Per-game profile does not serialize $field", "config.$field" in repository)
        }
    }

    @Test
    fun nativeShaderPreferencesFlowFromGlobalAndPerGameStorageToRendering() {
        val configHeader = cppSource("vita3k/vita3k/config/include/config/config.h")
        val stateHeader = cppSource("vita3k/vita3k/config/include/config/state.h")
        val settings = cppSource("vita3k/vita3k/config/src/settings.cpp")
        val runtime = cppSource("emucorev/src/runtime_bridge.cpp")
        val appInit = cppSource("vita3k/vita3k/app/src/app_init.cpp")
        val gui = cppSource("vita3k/vita3k/gui/src/gui.cpp")

        assertTrue("show-shader-cache-warn must be a serialized global option", "\"show-shader-cache-warn\"" in configHeader)
        assertTrue("Per-game config must carry the compile notice", "bool show_compile_shaders = true;" in stateHeader)
        assertTrue("Per-game config must carry the cache warning", "bool show_shader_cache_warn = true;" in stateHeader)
        listOf("show-compile-shaders", "show-shader-cache-warn").forEach { key ->
            assertTrue("Native custom config must read and write $key", settings.split(key).size >= 3)
        }
        assertTrue("Runtime toggle must update the effective profile", "current.show_compile_shaders =" in runtime)
        assertTrue("Renderer must consume the effective compile-notice value", "r.show_compile_shaders = cc.show_compile_shaders;" in appInit)
        assertTrue("Legacy GUI must consume the effective compile-notice value", "current_config.show_compile_shaders" in gui)
        assertTrue("Legacy GUI must consume the effective cache-warning value", "current_config.show_shader_cache_warn" in gui)
    }

    @Test
    fun accurateThreadSchedulingRoundTripsAndControlsTheGuestScheduler() {
        val configHeader = cppSource("vita3k/vita3k/config/include/config/config.h")
        val stateHeader = cppSource("vita3k/vita3k/config/include/config/state.h")
        val settings = cppSource("vita3k/vita3k/config/src/settings.cpp")
        val runtime = cppSource("vita3k/vita3k/interface.cpp")
        val thread = cppSource("vita3k/vita3k/kernel/src/thread.cpp")

        listOf("accurate-thread-scheduling", "guest-cores").forEach { key ->
            assertTrue("Global config must serialize $key", key in configHeader)
            assertTrue("Per-game config must read and write $key", settings.split(key).size >= 3)
        }
        assertTrue("Effective config must carry accurate scheduling", "accurate_thread_scheduling" in stateHeader)
        assertTrue("Effective config must carry the guest core count", "guest_cores" in stateHeader)
        assertTrue("Runtime initialization must apply the guest core limit", "guest_sched_set_cores" in runtime)
        assertTrue("Guest execution must be gated by the effective setting", "kernel.accurate_thread_scheduling" in thread)
    }

    private fun javaSource(relative: String): String = appModule()
        .resolve("src/main/java/com/sbro/emucorev")
        .resolve(relative)
        .readText()

    private fun cppSource(relative: String): String = appModule()
        .resolve("src/main/cpp")
        .resolve(relative)
        .readText()

    private fun appModule(): Path {
        val workingDirectory = Path.of(System.getProperty("user.dir"))
        return sequenceOf(workingDirectory, workingDirectory.resolve("app"))
            .firstOrNull { Files.isDirectory(it.resolve("src/main")) }
            ?: error("Unable to locate Android app module from $workingDirectory")
    }
}
