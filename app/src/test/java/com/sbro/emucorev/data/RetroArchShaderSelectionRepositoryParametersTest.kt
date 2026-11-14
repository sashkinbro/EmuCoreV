package com.sbro.emucorev.data

import android.content.Context
import android.app.Application
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], application = Application::class)
class RetroArchShaderSelectionRepositoryParametersTest {
    private lateinit var context: Application
    private lateinit var preset: RetroArchShaderPreset

    @Before
    fun setUp() {
        context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("retroarch_shader_selection", Context.MODE_PRIVATE)
            .edit().clear().commit()
        val presetFile = File(
            File(context.filesDir, "retroarch-shaders"),
            "parameter-test-${System.nanoTime()}/test.slangp"
        ).apply {
            parentFile?.mkdirs()
            writeText("# test preset")
        }
        preset = RetroArchShaderPreset(
            label = "parameter-test",
            absolutePath = presetFile.absolutePath,
            passCount = 1,
            textureIds = emptyList(),
            parameters = listOf(RetroArchShaderParameter("strength", "Strength", 0.5f, 0f, 1f, 0.1f))
        )
    }

    @Test
    fun valuesRoundTripByScopeResetIndependentlyAndClearWhenPresetIsRemoved() {
        val global = RetroArchShaderSelectionRepository(context)
        global.setGlobal(enabled = true, presetAbsolutePath = preset.absolutePath)
        global.setGameOverride("PCSA00001", enabled = true, presetAbsolutePath = preset.absolutePath)
        assertTrue(global.setParameterOverride(RetroArchShaderSelectionScope.GLOBAL, null, preset, "strength", 0.7f))
        assertTrue(global.setParameterOverride(RetroArchShaderSelectionScope.GAME_OVERRIDE, "PCSA00001", preset, "strength", 4f))
        assertFalse(global.setParameterOverride(RetroArchShaderSelectionScope.GAME_OVERRIDE, "PCSA00001", preset, "unknown", 0.2f))

        val reopened = RetroArchShaderSelectionRepository(context)
        assertEquals(mapOf("strength" to 0.7f), reopened.parameterOverrides(RetroArchShaderSelectionScope.GLOBAL, null, preset))
        assertEquals(mapOf("strength" to 1f), reopened.parameterOverrides(RetroArchShaderSelectionScope.GAME_OVERRIDE, "PCSA00001", preset))
        assertEquals(emptyMap<String, Float>(), reopened.parameterOverrides(RetroArchShaderSelectionScope.GAME_OVERRIDE, "PCSA00002", preset))

        reopened.clearParameterOverrides(RetroArchShaderSelectionScope.GAME_OVERRIDE, "PCSA00001", preset)
        assertEquals(emptyMap<String, Float>(), reopened.parameterOverrides(RetroArchShaderSelectionScope.GAME_OVERRIDE, "PCSA00001", preset))
        assertEquals(mapOf("strength" to 0.7f), reopened.parameterOverrides(RetroArchShaderSelectionScope.GLOBAL, null, preset))
        assertTrue(reopened.setParameterOverride(RetroArchShaderSelectionScope.GAME_OVERRIDE, "PCSA00001", preset, "strength", 0.9f))

        assertTrue(File(preset.absolutePath).delete())
        reopened.clearSelectionsForRemovedPresets(setOf(preset.absolutePath))
        assertFalse(reopened.global().enabled)
        assertFalse(reopened.effectiveFor("PCSA00001").enabled)
        assertTrue(reopened.effectiveFor("PCSA00001").inheritsGlobal)
        assertTrue(
            context.getSharedPreferences("retroarch_shader_selection", Context.MODE_PRIVATE)
                .all.keys.none { it.contains(".params.") }
        )
    }
}
