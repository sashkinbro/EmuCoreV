package com.sbro.emucorev

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.ui.Modifier
import androidx.compose.ui.Alignment
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import com.sbro.emucorev.core.vita.LaunchStateLoadingState
import com.sbro.emucorev.R
import com.sbro.emucorev.ui.emulation.LaunchStateLoadingOverlay
import com.sbro.emucorev.ui.theme.EmuCoreVTheme
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class LaunchStateLoadingOverlayTest {
    @get:Rule val compose = createComposeRule()

    @Test
    fun activeLoadShowsProgressAndBlocksTouchesToUnderlyingGameUi() {
        var backgroundClicks = 0
        compose.setContent {
            EmuCoreVTheme {
                Box(Modifier.fillMaxSize()) {
                    Button(
                        modifier = Modifier.align(Alignment.Center).testTag("underlay_action"),
                        onClick = { backgroundClicks++ }
                    ) { Text("Underlay") }
                    LaunchStateLoadingOverlay(
                        LaunchStateLoadingState(requested = true, active = true, progress = 0.42f)
                    )
                }
            }
        }

        val context = InstrumentationRegistry.getInstrumentation().targetContext
        compose.onNodeWithText(context.getString(R.string.emulation_savestate_startup_loading)).assertIsDisplayed()
        compose.onNodeWithTag("launch_state_loading_progress").assertIsDisplayed()
        compose.onNodeWithTag("launch_state_loading_overlay").performTouchInput { click(center) }
        assertEquals("The modal loading layer must consume taps instead of activating game/menu controls", 0, backgroundClicks)
    }

    @Test
    fun inactiveStateDoesNotLeaveLoadingLayerComposed() {
        compose.setContent {
            EmuCoreVTheme {
                LaunchStateLoadingOverlay(LaunchStateLoadingState())
            }
        }

        compose.onNodeWithTag("launch_state_loading_overlay").assertDoesNotExist()
    }
}
