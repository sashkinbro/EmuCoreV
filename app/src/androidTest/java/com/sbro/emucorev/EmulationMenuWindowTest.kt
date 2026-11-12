package com.sbro.emucorev

import android.view.View
import android.view.InputDevice
import android.view.MotionEvent
import android.os.SystemClock
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.PressInteraction
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.LocalRippleConfiguration
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.LocalContext
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
import androidx.lifecycle.findViewTreeLifecycleOwner
import androidx.lifecycle.findViewTreeViewModelStoreOwner
import androidx.lifecycle.setViewTreeLifecycleOwner
import androidx.lifecycle.setViewTreeViewModelStoreOwner
import androidx.savedstate.findViewTreeSavedStateRegistryOwner
import androidx.savedstate.setViewTreeSavedStateRegistryOwner
import java.util.concurrent.atomic.AtomicInteger

@OptIn(ExperimentalMaterial3Api::class)
class EmulationMenuWindowTest {
    @get:Rule val compose = createComposeRule()

    @Test fun softwareHostDisablesRipplesButAcceleratedMenuRestoresThem() {
        var softwareRippleEnabled = true
        var menuRippleEnabled = false
        compose.setContent {
            // An unattached Android View has the same software-rendering capability
            // as the Emulator Activity; the menu still creates a real accelerated window.
            val context = LocalContext.current
            val ownerView = LocalView.current
            val softwareView = androidx.compose.runtime.remember {
                View(context).apply {
                    setViewTreeLifecycleOwner(ownerView.findViewTreeLifecycleOwner())
                    setViewTreeViewModelStoreOwner(ownerView.findViewTreeViewModelStoreOwner())
                    setViewTreeSavedStateRegistryOwner(ownerView.findViewTreeSavedStateRegistryOwner())
                }
            }
            CompositionLocalProvider(LocalView provides softwareView) {
                EmuCoreVTheme {
                    val softwareConfiguration = LocalRippleConfiguration.current
                    SideEffect { softwareRippleEnabled = softwareConfiguration != null }
                    EmulationMenuWindow {
                        val menuConfiguration = LocalRippleConfiguration.current
                        SideEffect { menuRippleEnabled = menuConfiguration != null }
                        Text("Menu")
                    }
                }
            }
        }
        compose.runOnIdle {
            assertFalse("Software overlays must not start RenderNode ripple animators", softwareRippleEnabled)
            assertTrue("The accelerated menu must retain ripple feedback", menuRippleEnabled)
        }
    }

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

    @Test fun closingMenuWhilePointerIsPressedDisposesRippleBeforeWindowDetach() {
        val visible = mutableStateOf(true)
        val interactionSource = MutableInteractionSource()
        val pressCount = AtomicInteger(0)
        var menuView: View? = null
        compose.setContent {
            LaunchedEffect(interactionSource) {
                interactionSource.interactions.collect { interaction ->
                    if (interaction is PressInteraction.Press) pressCount.incrementAndGet()
                }
            }
            EmuCoreVTheme {
                if (visible.value) {
                    EmulationMenuWindow {
                        menuView = LocalView.current
                        Button(
                            onClick = {},
                            modifier = Modifier.testTag("pressed-menu-action"),
                            interactionSource = interactionSource
                        ) {
                            Text("Press")
                        }
                    }
                }
            }
        }

        compose.waitForIdle()
        val menuButton = compose.onNodeWithTag("pressed-menu-action")
        val bounds = menuButton.fetchSemanticsNode().boundsInRoot
        val downTime = SystemClock.uptimeMillis()
        menuButton.performTouchInput { down(center) }
        compose.waitUntil(timeoutMillis = 5_000) { pressCount.get() == 1 }
        compose.runOnIdle {
            assertEquals("Compose must receive DOWN before window teardown", 1, pressCount.get())
            visible.value = false
        }
        compose.waitForIdle()

        // The node is gone after close, so release the pointer through the captured root view.
        val up = MotionEvent.obtain(
            downTime,
            SystemClock.uptimeMillis(),
            MotionEvent.ACTION_UP,
            bounds.center.x,
            bounds.center.y,
            0
        ).apply { source = InputDevice.SOURCE_TOUCHSCREEN }
        compose.runOnIdle {
            val view = requireNotNull(menuView)
            try {
                view.dispatchTouchEvent(up)
            } finally {
                up.recycle()
            }
            assertFalse("The menu window should be removed after disposing its composition", view.isAttachedToWindow)
        }
    }
}
