package com.sbro.emucorev.ui.emulation

import android.view.KeyEvent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(manifest = Config.NONE, sdk = [29])
class MenuKeyRoutingTest {
    @Test
    fun dpadAndConfirmAreForwardedAndBackIsBack() {
        assertEquals(MenuKeyAction.Forward(KeyEvent.KEYCODE_DPAD_DOWN), routeMenuKey(KeyEvent.KEYCODE_DPAD_DOWN))
        assertEquals(MenuKeyAction.Forward(KeyEvent.KEYCODE_DPAD_CENTER), routeMenuKey(KeyEvent.KEYCODE_BUTTON_A))
        assertEquals(MenuKeyAction.Back, routeMenuKey(KeyEvent.KEYCODE_BUTTON_B))
        assertEquals(MenuKeyAction.Ignore, routeMenuKey(KeyEvent.KEYCODE_VOLUME_UP))
        assertEquals(MenuKeyAction.Ignore, routeMenuKey(KeyEvent.KEYCODE_BACK)) // hardware Back keeps its own path
    }

    @Test
    fun buttonAReachesTheMenuAsDpadCenterAndIsConsumed() {
        val delivered = mutableListOf<Int>()
        val consumed = dispatchMenuKey(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BUTTON_A), { delivered += it.keyCode; false }, { false })
        assertTrue(consumed)
        assertEquals(listOf(KeyEvent.KEYCODE_DPAD_CENTER), delivered)
    }

    @Test
    fun buttonBClosesOnKeyUpOnly() {
        var back = 0
        dispatchMenuKey(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BUTTON_B), { false }, { back++; true })
        assertEquals(0, back)
        assertTrue(dispatchMenuKey(KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_BUTTON_B), { false }, { back++; true }))
        assertEquals(1, back)
    }

    @Test
    fun unrelatedKeysAreLeftToTheGame() {
        assertFalse(dispatchMenuKey(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BUTTON_X), { true }, { true }))
    }
}
