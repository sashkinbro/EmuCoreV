package com.sbro.emucorev.ui.shaders

import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import com.sbro.emucorev.data.RetroArchShaderPreset
import com.sbro.emucorev.ui.theme.EmuCoreVTheme
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

class ShaderPresetPickerNavigationTest {
    @get:Rule val compose = createComposeRule()

    @Test fun returningFromGroupRestoresBothCategoryAndPresetScrollPositions() {
        val presets = (0 until 60).flatMap { group -> (0 until 40).map { preset ->
            val label = "group%03d/preset%03d".format(group, preset)
            RetroArchShaderPreset(label, "/$label.slangp", 1, emptyList(), emptyList())
        } }
        compose.setContent { EmuCoreVTheme {
            ShaderPresetPicker(presets, "", "None", "Global", false, {}, {}, null)
        } }
        fun position() = compose.onNodeWithTag("shader_picker_list").fetchSemanticsNode()
            .config[SemanticsProperties.VerticalScrollAxisRange].value()
        compose.onNodeWithTag("shader_picker_list").performScrollToIndex(35)
        val categoriesPosition = position()
        assertTrue(categoriesPosition > 30)
        compose.onNodeWithText("Group034  ·  40").performClick()
        compose.onNodeWithTag("shader_picker_list").performScrollToIndex(20)
        val presetsPosition = position()
        compose.onNodeWithTag("shader_group_back").performClick()
        assertEquals(categoriesPosition, position(), 0.1f)
        compose.onNodeWithText("Group034  ·  40").performClick()
        assertEquals(presetsPosition, position(), 0.1f)
    }
}
