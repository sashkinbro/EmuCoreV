package com.sbro.emucorev

import android.view.View
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.click
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performTouchInput
import com.sbro.emucorev.ui.emulation.EmulationMenuWindow
import com.sbro.emucorev.ui.theme.EmuCoreVTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class EmulationMenuWindowTest {
    @get:Rule val compose = createComposeRule()

    @Test fun menuUsesAcceleratedDrawingAndDisposesItsWindowOnClose() {
        val visible = mutableStateOf(true)
        var clicks = 0
        var menuView: View? = null
        compose.setContent {
            EmuCoreVTheme {
                if (visible.value) {
                    EmulationMenuWindow {
                        val view = LocalView.current
                        SideEffect { menuView = view }
                        Box(Modifier.fillMaxSize()) {
                            Button({ clicks++; visible.value = false }, Modifier.testTag("menu-window-action")) {
                                Text("Close")
                            }
                        }
                    }
                }
            }
        }
        repeat(3) {
            compose.waitForIdle()
            val view = requireNotNull(menuView)
            compose.runOnIdle {
                assertTrue("Menu must draw with hardware acceleration", view.isHardwareAccelerated)
                assertTrue(view.isAttachedToWindow)
            }
            compose.onNodeWithTag("menu-window-action").performTouchInput { click(center) }
            compose.waitForIdle()
            compose.runOnIdle {
                assertEquals(it + 1, clicks)
                assertFalse("Closing the menu must release its window", view.isAttachedToWindow)
                if (it < 2) visible.value = true
            }
        }
    }
}
