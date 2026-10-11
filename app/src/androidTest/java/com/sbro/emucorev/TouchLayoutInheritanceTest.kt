package com.sbro.emucorev

import android.content.Context
import android.content.ContextWrapper
import android.content.SharedPreferences
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.requiredSize
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.unit.dp
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import com.sbro.emucorev.ui.emulation.TouchControlElement
import com.sbro.emucorev.ui.emulation.TouchControlLayoutRepository
import com.sbro.emucorev.ui.emulation.TouchLayoutEditorScreen
import com.sbro.emucorev.ui.theme.EmuCoreVTheme
import org.junit.After
import org.junit.Before
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

class TouchLayoutInheritanceTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var isolatedContext: Context
    private lateinit var isolatedPreferences: SharedPreferences
    private lateinit var isolatedConfigPreferences: SharedPreferences
    private lateinit var runtimeRoot: File
    private lateinit var repository: TouchControlLayoutRepository

    @Before fun setUp() {
        val targetContext = InstrumentationRegistry.getInstrumentation().targetContext
        isolatedPreferences = targetContext.getSharedPreferences(TEST_PREFERENCES_NAME, Context.MODE_PRIVATE)
        isolatedPreferences.edit().clear().commit()
        isolatedConfigPreferences = targetContext.getSharedPreferences(TEST_CONFIG_PREFERENCES_NAME, Context.MODE_PRIVATE)
        isolatedConfigPreferences.edit().clear().commit()
        runtimeRoot = File(targetContext.cacheDir, "touch-layout-editor-instrumentation-test").apply {
            deleteRecursively()
            mkdirs()
        }
        isolatedContext = TouchLayoutTestContext(targetContext, runtimeRoot)
        repository = TouchControlLayoutRepository(isolatedContext)
    }

    @After fun tearDown() {
        isolatedPreferences.edit().clear().commit()
        isolatedConfigPreferences.edit().clear().commit()
        runtimeRoot.deleteRecursively()
    }

    @Test fun editingGameLayoutCreatesOnlyAnOverrideAndUseGlobalRemovesIt() {
        val global = listOf(TouchControlElement("cross", .2f, .3f, .08f, .1f))
        repository.saveGlobal(global)

        compose.setContent {
            CompositionLocalProvider(LocalContext provides isolatedContext) {
                EmuCoreVTheme {
                    TouchLayoutEditorScreen(titleId = "PCSE00333", onBackClick = {})
                }
            }
        }

        compose.onNodeWithTag("touch_control_cross").performTouchInput { click() }
        compose.onNodeWithTag("controls_editor_duplicate").performClick()
        compose.waitForIdle()
        assertTrue(repository.hasGameOverride("PCSE00333"))
        assertEquals(global, repository.loadGlobal())

        compose.onNodeWithTag("touch_layout_use_global").assertDoesNotExist()
        repository.clearForGame("PCSE00333")
        assertFalse(repository.hasGameOverride("PCSE00333"))
        assertEquals(global, repository.loadEffective("PCSE00333"))
    }

    @Test fun standaloneEntryShowsSharedFullscreenEditorWithoutRouteChrome() {
        compose.setContent {
            Box(Modifier.requiredSize(720.dp, 500.dp)) {
                CompositionLocalProvider(LocalContext provides isolatedContext) {
                    EmuCoreVTheme {
                        TouchLayoutEditorScreen(onBackClick = {})
                    }
                }
            }
        }
        compose.waitForIdle()
        compose.onNodeWithTag("controls_editor_panel").assertIsDisplayed()
        compose.onNodeWithTag("touch_control_cross").assertIsDisplayed()
        compose.onNodeWithTag("touch_layout_scope_status").assertDoesNotExist()
        compose.onNodeWithTag("touch_layout_editor_hint").assertDoesNotExist()
        compose.onNodeWithTag("touch_layout_use_global").assertDoesNotExist()
        compose.onNodeWithTag("touch_layout_preview_canvas").assertDoesNotExist()
    }

    private class TouchLayoutTestContext(base: Context, private val runtimeRoot: File) : ContextWrapper(base) {
        override fun getApplicationContext(): Context = this

        override fun getSharedPreferences(name: String?, mode: Int): SharedPreferences {
            return super.getSharedPreferences(
                when (name) {
                    "touch_control_layout" -> TEST_PREFERENCES_NAME
                    "emucorev_ui_config" -> TEST_CONFIG_PREFERENCES_NAME
                    else -> name
                },
                mode
            )
        }

        override fun getExternalFilesDir(type: String?): File = runtimeRoot
    }

    companion object {
        private const val TEST_PREFERENCES_NAME = "touch_control_layout_instrumentation_test"
        private const val TEST_CONFIG_PREFERENCES_NAME = "touch_layout_editor_config_instrumentation_test"
    }
}
