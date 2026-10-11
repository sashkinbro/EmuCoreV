package com.sbro.emucorev

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.ui.Modifier
import androidx.compose.ui.Alignment
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toPixelMap
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.unit.dp
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
                Box(Modifier.fillMaxSize().background(Color.Green)) {
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
        val coveredPixel = compose.onNodeWithTag("launch_state_loading_overlay")
            .captureToImage().toPixelMap()[0, 0]
        assertEquals("Startup loading must continue to cover stale content", 0f, coveredPixel.green, 0.01f)
        compose.onNodeWithTag("launch_state_loading_overlay").performTouchInput { click(center) }
        assertEquals("The modal loading layer must consume taps instead of activating game/menu controls", 0, backgroundClicks)
    }

    @Test
    fun savingShowsLocalizedIndeterminateProgressAndBlocksUnderlyingControls() {
        var backgroundClicks = 0
        compose.setContent {
            EmuCoreVTheme {
                Box(Modifier.fillMaxSize().background(Color.Green)) {
                    Button(
                        modifier = Modifier.align(Alignment.TopStart).padding(24.dp).testTag("saving_underlay_action"),
                        onClick = { backgroundClicks++ }
                    ) { Text("Underlay") }
                    LaunchStateLoadingOverlay(
                        state = LaunchStateLoadingState(active = true),
                        indeterminate = true,
                        scrimAlpha = 0.35f,
                        messageRes = R.string.emulation_savestate_saving
                    )
                }
            }
        }
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        compose.onNodeWithText(context.getString(R.string.emulation_savestate_saving)).assertIsDisplayed()
        compose.onNodeWithTag("launch_state_loading_progress").assertIsDisplayed()
        val exposedGamePixel = compose.onNodeWithTag("launch_state_loading_overlay")
            .captureToImage().toPixelMap()[0, 0]
        assertEquals("Saving must dim the game frame by the 35% scrim", 0.65f, exposedGamePixel.green, 0.06f)
        val underlayBounds = compose.onNodeWithTag("saving_underlay_action").fetchSemanticsNode().boundsInRoot
        val overlayBounds = compose.onNodeWithTag("launch_state_loading_overlay").fetchSemanticsNode().boundsInRoot
        compose.onNodeWithTag("launch_state_loading_overlay").performTouchInput {
            click(Offset(underlayBounds.center.x - overlayBounds.left, underlayBounds.center.y - overlayBounds.top))
        }
        assertEquals("The full scrim must intercept taps outside the progress card", 0, backgroundClicks)
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
