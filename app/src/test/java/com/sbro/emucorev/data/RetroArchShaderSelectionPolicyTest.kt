package com.sbro.emucorev.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class RetroArchShaderSelectionPolicyTest {
    @Test
    fun missingGameOverrideInheritsTheGlobalSelection() {
        val global = RetroArchShaderSelection(
            enabled = true,
            presetPath = "/pack/global.slangp",
            scope = RetroArchShaderSelectionScope.GLOBAL,
            inheritsGlobal = false
        )

        val effective = RetroArchShaderSelectionPolicy.effective(global, null)

        assertEquals(global.presetPath, effective.presetPath)
        assertTrue(effective.enabled)
        assertEquals(RetroArchShaderSelectionScope.GLOBAL, effective.scope)
        assertTrue(effective.inheritsGlobal)
    }

    @Test
    fun explicitGameDisableOverridesAnEnabledGlobalSelection() {
        val global = RetroArchShaderSelection(
            enabled = true,
            presetPath = "/pack/global.slangp",
            scope = RetroArchShaderSelectionScope.GLOBAL,
            inheritsGlobal = false
        )
        val disabled = RetroArchShaderSelection(
            enabled = false,
            presetPath = "",
            scope = RetroArchShaderSelectionScope.GAME_OVERRIDE,
            inheritsGlobal = false
        )

        val effective = RetroArchShaderSelectionPolicy.effective(global, disabled)

        assertFalse(effective.enabled)
        assertEquals("", effective.presetPath)
        assertFalse(effective.inheritsGlobal)
        assertEquals(RetroArchShaderSelectionScope.GAME_OVERRIDE, effective.scope)
    }

    @Test
    fun parameterOverridesAreRestrictedToKnownFiniteValuesAndClamped() {
        val preset = RetroArchShaderPreset(
            label = "test",
            absolutePath = "/pack/test.slangp",
            passCount = 1,
            textureIds = emptyList(),
            parameters = listOf(
                RetroArchShaderParameter("strength", "Strength", 0.5f, 0f, 1f, 0.1f),
                RetroArchShaderParameter("enabled", "Enabled", 1f, 0f, 1f, 1f)
            )
        )

        val actual = RetroArchShaderSelectionPolicy.validParameterOverrides(
            preset,
            mapOf("strength" to 3f, "enabled" to Float.NaN, "unknown" to 0.7f)
        )

        assertEquals(mapOf("strength" to 1f), actual)
    }
}
