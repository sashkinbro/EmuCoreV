package com.sbro.emucorev.ui.emulation

import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorev.R
import com.sbro.emucorev.data.SaveStateAutomationPreferences
import com.sbro.emucorev.ui.theme.EmuCoreVTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.util.UUID

class SaveStateAutomationSectionTest {
    @get:Rule val compose = createComposeRule()

    @Test fun actualMenuOptionsAreIndependentForEachSelectedGame() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val a = "AUTO_TEST_A_${UUID.randomUUID()}"
        val b = "AUTO_TEST_B_${UUID.randomUUID()}"
        val selected = mutableStateOf(a)
        try {
            compose.setContent { EmuCoreVTheme { SaveStateAutomationSection(selected.value) } }
            compose.onNodeWithText(context.getString(R.string.emulation_auto_save_on_exit)).performClick()
            compose.onNodeWithText(context.getString(R.string.emulation_auto_load_on_start)).performClick()
            SaveStateAutomationPreferences(context, a).use {
                assertTrue(it.current.autoSaveOnExit)
                assertTrue(it.current.autoLoadOnStart)
            }
            compose.runOnIdle { selected.value = b }
            compose.waitForIdle()
            SaveStateAutomationPreferences(context, b).use {
                assertFalse(it.current.autoSaveOnExit)
                assertFalse(it.current.autoLoadOnStart)
            }
            compose.onNodeWithText(context.getString(R.string.emulation_auto_load_on_start)).performClick()
            SaveStateAutomationPreferences(context, b).use {
                assertFalse(it.current.autoSaveOnExit)
                assertTrue(it.current.autoLoadOnStart)
            }
            SaveStateAutomationPreferences(context, a).use { assertTrue(it.current.autoSaveOnExit) }
        } finally {
            SaveStateAutomationPreferences(context, a).use { it.reset() }
            SaveStateAutomationPreferences(context, b).use { it.reset() }
        }
    }
}
