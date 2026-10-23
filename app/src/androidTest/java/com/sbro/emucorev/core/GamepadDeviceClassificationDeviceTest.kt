package com.sbro.emucorev.core

import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyEvent
import com.sbro.emucorev.core.input.InputDeviceClassifier
import org.libsdl.app.SDLControllerManager
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Opt-in: feed androidTest/assets/input/controller-classification.jsonl to `adb shell hid -`
 * and keep stdin open. Pass -e hidDeviceFixtures true. No game or physical pad is required.
 * Synthetic descriptors cover classification; they do not reproduce specific Ipega firmware.
 */
@RunWith(AndroidJUnit4::class)
class GamepadDeviceClassificationDeviceTest {
    @Before fun requireFixtures() {
        assumeTrue(InstrumentationRegistry.getArguments().getString("hidDeviceFixtures") == "true")
    }

    @Test fun consumerControlNameDoesNotRejectControllerCapabilities() =
        assertController("EmuCoreX Test Consumer Control")

    @Test fun systemControlNameDoesNotRejectControllerCapabilities() =
        assertController("EmuCoreX Test System Control")

    @Test fun usbControllerStillUsesControllerPath() =
        assertController("EmuCoreX Test USB Gamepad")

    @Test fun nonControllerDevicesStayOutsideControllerPath() {
        listOf("EmuCoreX Test Media Consumer Control", "EmuCoreX Test Keyboard",
            "EmuCoreX Test Mouse", "uinput-fpc EmuCoreX Test").forEach {
            val device = device(it)
            assertFalse(it, InputDeviceClassifier.isPhysicalGameController(device.id))
        }
    }

    @Test fun controllerButtonsCannotBeCapturedAsKeyboardBindings() = onMain {
        var captured: Int? = null
        KeyboardMouseManager.startKeyboardBindingCapture(0, "cross") { captured = it }
        try {
            assertFalse(KeyboardMouseManager.handleKeyBindingCapture(
                key(device("EmuCoreX Test Consumer Control"), KeyEvent.KEYCODE_BUTTON_A, InputDevice.SOURCE_GAMEPAD)))
            assertNull(captured)
        } finally {
            KeyboardMouseManager.cancelBindingCapture()
        }
    }

    @Test fun actualKeyboardBindingsStillCaptureKeys() = onMain {
        var captured: Int? = null
        KeyboardMouseManager.startKeyboardBindingCapture(0, "cross") { captured = it }
        try {
            assertTrue(KeyboardMouseManager.handleKeyBindingCapture(
                key(device("EmuCoreX Test Keyboard"), KeyEvent.KEYCODE_A, InputDevice.SOURCE_KEYBOARD)))
            assertEquals(KeyEvent.KEYCODE_A, captured)
        } finally {
            KeyboardMouseManager.cancelBindingCapture()
        }
    }

    private fun assertController(name: String) {
        val device = device(name)
        assertFalse(device.isVirtual)
        assertTrue(device.supportsSource(InputDevice.SOURCE_GAMEPAD))
        assertTrue(name, InputDeviceClassifier.isPhysicalGameController(device.id))
        assertTrue("SDL must also accept the controller", SDLControllerManager.isDeviceSDLJoystick(device.id))
    }

    private fun device(name: String): InputDevice = InputDevice.getDeviceIds().asSequence()
        .mapNotNull { InputDevice.getDevice(it) }.firstOrNull { it.name == name }
        ?: throw AssertionError("Missing required UHID fixture: $name")

    private fun key(device: InputDevice, code: Int, source: Int) =
        KeyEvent(0L, SystemClock.uptimeMillis(), KeyEvent.ACTION_DOWN, code, 0, 0, device.id, 0, 0, source)

    private fun onMain(action: () -> Unit) =
        InstrumentationRegistry.getInstrumentation().runOnMainSync(action)
}
