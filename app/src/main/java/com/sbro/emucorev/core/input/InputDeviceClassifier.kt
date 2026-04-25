package com.sbro.emucorev.core.input

import android.view.InputDevice
import android.view.KeyEvent

object InputDeviceClassifier {
    private val gamepadButtonKeys = intArrayOf(
        KeyEvent.KEYCODE_BUTTON_A,
        KeyEvent.KEYCODE_BUTTON_B,
        KeyEvent.KEYCODE_BUTTON_X,
        KeyEvent.KEYCODE_BUTTON_Y,
        KeyEvent.KEYCODE_BUTTON_L1,
        KeyEvent.KEYCODE_BUTTON_R1,
        KeyEvent.KEYCODE_BUTTON_L2,
        KeyEvent.KEYCODE_BUTTON_R2,
        KeyEvent.KEYCODE_BUTTON_THUMBL,
        KeyEvent.KEYCODE_BUTTON_THUMBR,
        KeyEvent.KEYCODE_BUTTON_START,
        KeyEvent.KEYCODE_BUTTON_SELECT,
        KeyEvent.KEYCODE_BUTTON_MODE
    )

    private val blockedControllerIdentityParts = listOf(
        "uinput-fpc",
        "uinput_fpc",
        "uinput_goodix",
        "uinput-goodix",
        "fpc",
        "goodix",
        "fingerprint",
        "finger print",
        "fp_",
        "fp-"
    )

    fun isPhysicalGameController(device: InputDevice?): Boolean {
        if (device == null || device.id < 0 || device.isVirtual) {
            return false
        }
        if (looksLikeFingerprintDevice(device)) {
            return false
        }

        val sources = device.sources
        val hasJoystickSource = (sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK ||
            (sources and InputDevice.SOURCE_CLASS_JOYSTICK) == InputDevice.SOURCE_CLASS_JOYSTICK
        val hasGamepadSource = (sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
        val hasDpadSource = (sources and InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD

        if (!hasJoystickSource && !hasGamepadSource && !hasDpadSource) {
            return false
        }

        return hasJoystickAxes(device) || hasGamepadButtons(device)
    }

    private fun looksLikeFingerprintDevice(device: InputDevice): Boolean {
        val identity = buildString {
            append(device.name.orEmpty())
            append(' ')
            append(device.descriptor.orEmpty())
        }.lowercase()

        return blockedControllerIdentityParts.any { part -> identity.contains(part) }
    }

    private fun hasJoystickAxes(device: InputDevice): Boolean {
        return device.motionRanges.count { range ->
            (range.source and InputDevice.SOURCE_CLASS_JOYSTICK) == InputDevice.SOURCE_CLASS_JOYSTICK
        } >= 2
    }

    private fun hasGamepadButtons(device: InputDevice): Boolean {
        return runCatching {
            device.hasKeys(*gamepadButtonKeys).any { it }
        }.getOrDefault(false)
    }
}
