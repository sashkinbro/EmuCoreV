package com.sbro.emucorev.core.input

import android.view.InputDevice
import android.view.KeyEvent

object InputDeviceClassifier {
    private val faceAndShoulderButtonKeys = intArrayOf(
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

    private val dpadKeys = intArrayOf(
        KeyEvent.KEYCODE_DPAD_UP,
        KeyEvent.KEYCODE_DPAD_DOWN,
        KeyEvent.KEYCODE_DPAD_LEFT,
        KeyEvent.KEYCODE_DPAD_RIGHT,
        KeyEvent.KEYCODE_DPAD_CENTER
    )

    private val gamepadButtonKeys = faceAndShoulderButtonKeys + dpadKeys

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

    private val controllerIdentityParts = listOf(
        "8bitdo",
        "bluetooth gamepad",
        "controller",
        "dualshock",
        "dualsense",
        "game controller",
        "gamepad",
        "gamesir",
        "ipega",
        "ps3",
        "ps4",
        "ps5",
        "x-box",
        "xbox"
    )

    private val physicalControllerCache = java.util.concurrent.ConcurrentHashMap<Int, Boolean>()
    private val controllerIdentityCache = java.util.concurrent.ConcurrentHashMap<Int, Boolean>()
    private val fingerprintIdentityCache = java.util.concurrent.ConcurrentHashMap<Int, Boolean>()

    fun invalidateDevice(deviceId: Int) {
        physicalControllerCache.remove(deviceId)
        controllerIdentityCache.remove(deviceId)
        fingerprintIdentityCache.remove(deviceId)
    }

    fun isPhysicalGameController(deviceId: Int): Boolean {
        if (deviceId < 0) return false
        return physicalControllerCache.getOrPut(deviceId) {
            val device = InputDevice.getDevice(deviceId) ?: return@getOrPut false
            if (device.isVirtual) return@getOrPut false
            determineIsPhysicalGameController(device)
        }
    }

    private fun determineIsPhysicalGameController(device: InputDevice): Boolean {
        if (device.id < 0 || device.isVirtual) {
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

        if (hasJoystickAxes(device) || hasGamepadButtons(device)) {
            return true
        }

        return ((hasJoystickSource || hasGamepadSource) ||
            (hasDpadSource && device.isExternal)) &&
            looksLikeControllerDevice(device)
    }

    fun isGameControllerKeyEvent(deviceId: Int, source: Int, keyCode: Int): Boolean {
        if (!isGamepadKeyCode(keyCode)) {
            return false
        }
        val device = InputDevice.getDevice(deviceId) ?: return false
        if (device.id < 0 || device.isVirtual || looksLikeFingerprintDevice(device)) {
            return false
        }

        val resolvedSource = if (source != InputDevice.SOURCE_UNKNOWN) source else device.sources
        val hasControllerSource = (resolvedSource and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
            (resolvedSource and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK ||
            (resolvedSource and InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD
        if (!hasControllerSource) {
            return false
        }

        return isPhysicalGameController(deviceId) ||
            (device.isExternal && looksLikeControllerDevice(device)) ||
            ((resolvedSource and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD && looksLikeControllerDevice(device))
    }

    private fun looksLikeFingerprintDevice(device: InputDevice): Boolean {
        return fingerprintIdentityCache.getOrPut(device.id) {
            val identity = buildString {
                append(device.name.orEmpty())
                append(' ')
                append(device.descriptor.orEmpty())
            }.lowercase()

            blockedControllerIdentityParts.any { part -> identity.contains(part) }
        }
    }

    private fun looksLikeControllerDevice(device: InputDevice): Boolean {
        return controllerIdentityCache.getOrPut(device.id) {
            val identity = buildString {
                append(device.name.orEmpty())
                append(' ')
                append(device.descriptor.orEmpty())
            }.lowercase()

            controllerIdentityParts.any { part -> identity.contains(part) }
        }
    }

    private fun hasJoystickAxes(device: InputDevice): Boolean {
        return device.motionRanges.count { range ->
            (range.source and InputDevice.SOURCE_CLASS_JOYSTICK) == InputDevice.SOURCE_CLASS_JOYSTICK
        } >= 2
    }

    private fun hasGamepadButtons(device: InputDevice): Boolean {
        return runCatching {
            val sources = device.sources
            val hasGamepadSource = (sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
                (sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
            val keys = if (hasGamepadSource) gamepadButtonKeys else faceAndShoulderButtonKeys
            device.hasKeys(*keys).any { it }
        }.getOrDefault(false)
    }

    private fun isGamepadKeyCode(keyCode: Int): Boolean {
        return gamepadButtonKeys.contains(keyCode)
    }
}
