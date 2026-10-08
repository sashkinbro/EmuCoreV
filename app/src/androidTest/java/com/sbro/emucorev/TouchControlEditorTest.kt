package com.sbro.emucorev

import android.graphics.Bitmap
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.requiredSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorev.data.TouchControlPressEffect
import com.sbro.emucorev.data.TouchControlVisualStyle
import com.sbro.emucorev.ui.emulation.OnScreenControls
import com.sbro.emucorev.ui.emulation.TouchControlElement
import com.sbro.emucorev.ui.theme.EmuCoreVTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import com.sbro.emucorev.core.vita.overlay.InputOverlay

class TouchControlEditorTest {
    @get:Rule val compose = createComposeRule()
    private val events = mutableListOf<Pair<Int, Boolean>>()
    private var layout: List<TouchControlElement>? by mutableStateOf(null)

    private fun screenshot(name: String) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        File(context.cacheDir, name).outputStream().use {
            compose.onRoot().captureToImage().asAndroidBitmap().compress(Bitmap.CompressFormat.PNG, 100, it)
        }
    }

    private fun showEditor(edit: Boolean, compact: Boolean = false) {
        compose.setContent {
            EmuCoreVTheme {
                Box(if (compact) Modifier.requiredSize(800.dp, 360.dp) else Modifier.fillMaxSize()) {
                OnScreenControls(1f, 80, true, 0, TouchControlVisualStyle.CLASSIC,
                    TouchControlPressEffect.GLOW, false, 0, 100, edit, layout,
                    { layout = it }, {}, {}, {}, { id, held -> events += id to held }, { _, _ -> },
                    Modifier.fillMaxSize())
                }
            }
        }
    }

    @Test fun shortLandscapeEditorCanScrollToItsHeightControls() {
        showEditor(true, compact = true)
        compose.onNodeWithTag("controls_editor_height_row")
            .performScrollTo().assertIsDisplayed()
        val row = compose.onNodeWithTag("controls_editor_height_row").fetchSemanticsNode().boundsInRoot
        val panel = compose.onNodeWithTag("controls_editor_panel").fetchSemanticsNode().boundsInRoot
        assertTrue(row.bottom <= panel.bottom + 1f && row.top >= panel.top - 1f)
        screenshot("controls-editor-short-landscape.png")
    }

    @Test fun hidingOneFaceButtonKeepsOtherFaceButtonsWorking() {
        layout = listOf(TouchControlElement("cross", .6f, .6f, .08f, .12f),
            TouchControlElement("triangle", .6f, .3f, .08f, .12f, visible = false))
        showEditor(false)
        compose.onNodeWithTag("touch_control_cross").performTouchInput { click() }
        compose.runOnIdle { assertEquals(listOf(InputOverlay.ControlId.a to true, InputOverlay.ControlId.a to false), events) }
    }

    @Test fun slidingAcrossFaceGroupReleasesTheOldButtonAndPressesTheNewOne() {
        layout = listOf(
            TouchControlElement("triangle", .70f, .50f, .06f, .09f),
            TouchControlElement("cross", .70f, .70f, .06f, .09f),
            TouchControlElement("square", .60f, .60f, .06f, .09f),
            TouchControlElement("circle", .80f, .60f, .06f, .09f))
        showEditor(false)
        val from = compose.onNodeWithTag("touch_control_cross").fetchSemanticsNode().boundsInRoot.center
        val to = compose.onNodeWithTag("touch_control_circle").fetchSemanticsNode().boundsInRoot.center
        compose.onRoot().performTouchInput { down(from); moveTo(to); up() }
        compose.runOnIdle {
            assertEquals(listOf(InputOverlay.ControlId.a to true, InputOverlay.ControlId.a to false,
                InputOverlay.ControlId.b to true, InputOverlay.ControlId.b to false), events)
        }
    }

    @Test fun comboAboveFaceGroupReceivesBothActionsAndReleasesThem() {
        layout = listOf(
            TouchControlElement("triangle", .70f, .50f, .06f, .09f),
            TouchControlElement("cross", .70f, .70f, .06f, .09f),
            TouchControlElement("square", .60f, .60f, .06f, .09f),
            TouchControlElement("circle", .80f, .60f, .06f, .09f),
            TouchControlElement("custom_combo", .70f, .60f, .06f, .09f, actionId = "cross", secondaryActionId = "l1"))
        showEditor(false)
        compose.onNodeWithTag("touch_control_custom_combo").performTouchInput { click() }
        compose.runOnIdle {
            assertEquals(setOf(InputOverlay.ControlId.a to true, InputOverlay.ControlId.l1 to true,
                InputOverlay.ControlId.a to false, InputOverlay.ControlId.l1 to false), events.toSet())
            assertEquals(4, events.size)
        }
    }

    @Test fun duplicateAndComboEditorUpdateTheActualLayout() {
        showEditor(true)
        screenshot("controls-editor-default.png")
        compose.onNodeWithTag("controls_editor_duplicate").performClick()
        compose.runOnIdle {
            assertEquals(1, layout!!.count { it.id.startsWith("custom_") })
            assertEquals("l2", layout!!.last().actionId)
        }
        compose.onNodeWithTag("controls_editor_combo").performClick()
        compose.onNodeWithTag("combo_primary_cross").performClick()
        compose.onNodeWithTag("combo_secondary_l1").performScrollTo().performClick()
        compose.onNodeWithTag("combo_confirm").performClick()
        compose.runOnIdle {
            assertEquals("cross", layout!!.last().actionId)
            assertEquals("l1", layout!!.last().secondaryActionId)
        }
        screenshot("controls-editor-combo.png")
    }
}
