package com.sbro.emucorev.core

import android.view.KeyEvent
import kotlin.math.sqrt

/**
 * Pure mapping rules shared by the keyboard/mouse settings UI, the live input
 * dispatcher and local unit tests. Keeping Android framework calls out of this
 * object (apart from key-code constants) lets the rules run on the JVM.
 */
object KeyboardMouseBindings {
    const val UNBOUND = -1

    const val MOUSE_BUTTON_LEFT = 0
    const val MOUSE_BUTTON_RIGHT = 1
    const val MOUSE_BUTTON_MIDDLE = 2
    const val MOUSE_BUTTON_BACK = 3
    const val MOUSE_BUTTON_FORWARD = 4

    const val MOUSE_AIM_OFF = 0
    const val MOUSE_AIM_RIGHT_STICK = 1
    const val MOUSE_AIM_LEFT_STICK = 2

    const val MOUSE_WHEEL_OFF = 0
    const val MOUSE_WHEEL_DPAD = 1
    const val MOUSE_WHEEL_TRIGGERS = 2
    const val MOUSE_WHEEL_LEFT_STICK = 3
    const val MOUSE_WHEEL_RIGHT_STICK = 4
    const val MOUSE_WHEEL_SHOULDERS = 5

    const val DEFAULT_MOUSE_SENSITIVITY = 100
    const val MOUSE_SENSITIVITY_MIN = 10
    const val MOUSE_SENSITIVITY_MAX = 300

    const val DEFAULT_MOUSE_VERTICAL_SENSITIVITY = 100
    const val MOUSE_VERTICAL_SENSITIVITY_MIN = 10
    const val MOUSE_VERTICAL_SENSITIVITY_MAX = 300

    const val DEFAULT_MOUSE_DEADZONE = 2
    const val MOUSE_DEADZONE_MIN = 0
    const val MOUSE_DEADZONE_MAX = 30

    const val DEFAULT_MOUSE_WHEEL_PULSE_MS = 90
    const val MOUSE_WHEEL_PULSE_MIN_MS = 30
    const val MOUSE_WHEEL_PULSE_MAX_MS = 300

    // SceCtrlButtons masks (Vita). The native bridge injects this bitmask into
    // the controller state for the selected player.
    const val PAD_BUTTON_SELECT = 0x00000001
    const val PAD_BUTTON_L3 = 0x00000002
    const val PAD_BUTTON_R3 = 0x00000004
    const val PAD_BUTTON_START = 0x00000008
    const val PAD_BUTTON_UP = 0x00000010
    const val PAD_BUTTON_RIGHT = 0x00000020
    const val PAD_BUTTON_DOWN = 0x00000040
    const val PAD_BUTTON_LEFT = 0x00000080
    const val PAD_BUTTON_L2 = 0x00000100
    const val PAD_BUTTON_R2 = 0x00000200
    const val PAD_BUTTON_L1 = 0x00000400
    const val PAD_BUTTON_R1 = 0x00000800
    const val PAD_BUTTON_TRIANGLE = 0x00001000
    const val PAD_BUTTON_CIRCLE = 0x00002000
    const val PAD_BUTTON_CROSS = 0x00004000
    const val PAD_BUTTON_SQUARE = 0x00008000

    private const val MOUSE_BASE_SCALE = 0.045f

    val keyboardDefaults: Map<String, Int> = mapOf(
        "dpad_up" to KeyEvent.KEYCODE_DPAD_UP,
        "dpad_down" to KeyEvent.KEYCODE_DPAD_DOWN,
        "dpad_left" to KeyEvent.KEYCODE_DPAD_LEFT,
        "dpad_right" to KeyEvent.KEYCODE_DPAD_RIGHT,
        "left_stick_up" to KeyEvent.KEYCODE_W,
        "left_stick_right" to KeyEvent.KEYCODE_D,
        "left_stick_down" to KeyEvent.KEYCODE_S,
        "left_stick_left" to KeyEvent.KEYCODE_A,
        "right_stick_up" to KeyEvent.KEYCODE_T,
        "right_stick_right" to KeyEvent.KEYCODE_H,
        "right_stick_down" to KeyEvent.KEYCODE_G,
        "right_stick_left" to KeyEvent.KEYCODE_F,
        "start" to KeyEvent.KEYCODE_ENTER,
        "select" to KeyEvent.KEYCODE_DEL,
        "triangle" to KeyEvent.KEYCODE_I,
        "circle" to KeyEvent.KEYCODE_L,
        "cross" to KeyEvent.KEYCODE_K,
        "square" to KeyEvent.KEYCODE_J,
        "l1" to KeyEvent.KEYCODE_Q,
        "l2" to KeyEvent.KEYCODE_1,
        "l3" to KeyEvent.KEYCODE_2,
        "r1" to KeyEvent.KEYCODE_E,
        "r2" to KeyEvent.KEYCODE_3,
        "r3" to KeyEvent.KEYCODE_4
    )

    val mouseDefaults: Map<String, Int> = mapOf(
        "cross" to MOUSE_BUTTON_LEFT,
        "circle" to MOUSE_BUTTON_RIGHT,
        "square" to MOUSE_BUTTON_MIDDLE
    )

    fun resolveKeyboardBinding(
        actionId: String,
        customBindings: Map<String, Int>
    ): Int? {
        if (customBindings.containsKey(actionId)) {
            return customBindings[actionId]?.takeIf { it >= 0 }
        }
        return keyboardDefaults[actionId]
    }

    fun resolveMouseBinding(
        actionId: String,
        customBindings: Map<String, Int>
    ): Int? {
        if (customBindings.containsKey(actionId)) {
            return customBindings[actionId]?.takeIf { it >= 0 }
        }
        return mouseDefaults[actionId]
    }

    fun assign(bindings: Map<String, Int>, actionId: String, code: Int): Map<String, Int> {
        val updated = bindings.toMutableMap()
        if (code < 0) {
            updated[actionId] = UNBOUND
            return updated
        }
        updated.entries.removeAll { (existingAction, existingCode) ->
            existingAction != actionId && existingCode == code
        }
        updated[actionId] = code
        return updated
    }

    fun clear(bindings: Map<String, Int>, actionId: String): Map<String, Int> {
        return bindings + (actionId to UNBOUND)
    }

    fun normalizeMouseDelta(
        dx: Float,
        dy: Float,
        sensitivityPercent: Int,
        verticalSensitivityPercent: Int,
        invertX: Boolean,
        invertY: Boolean,
        deadzonePercent: Int
    ): Pair<Float, Float> {
        if (dx == 0f && dy == 0f) return 0f to 0f
        val sensitivity = sensitivityPercent
            .coerceIn(MOUSE_SENSITIVITY_MIN, MOUSE_SENSITIVITY_MAX) / 100f
        val verticalSensitivity = verticalSensitivityPercent
            .coerceIn(MOUSE_VERTICAL_SENSITIVITY_MIN, MOUSE_VERTICAL_SENSITIVITY_MAX) / 100f
        val scale = MOUSE_BASE_SCALE * sensitivity
        val x = (dx * scale * if (invertX) -1f else 1f).coerceIn(-1f, 1f)
        val y = (dy * scale * verticalSensitivity * if (invertY) -1f else 1f)
            .coerceIn(-1f, 1f)
        val magnitude = sqrt(x * x + y * y)
        if (magnitude == 0f) return 0f to 0f
        val deadzone = deadzonePercent.coerceIn(MOUSE_DEADZONE_MIN, MOUSE_DEADZONE_MAX) / 100f
        if (magnitude <= deadzone) return 0f to 0f
        val adjustedMagnitude = (magnitude - deadzone) / (1f - deadzone)
        val adjustedScale = adjustedMagnitude / magnitude
        return (x * adjustedScale).coerceIn(-1f, 1f) to
            (y * adjustedScale).coerceIn(-1f, 1f)
    }

    fun smoothAimValue(current: Float, target: Float, smoothingPercent: Int): Float {
        val smoothing = smoothingPercent.coerceIn(0, 90) / 100f
        if (smoothing == 0f) return target
        return current * smoothing + target * (1f - smoothing)
    }

    fun swapMouseButton(button: Int, leftHanded: Boolean): Int {
        if (!leftHanded) return button
        return when (button) {
            MOUSE_BUTTON_LEFT -> MOUSE_BUTTON_RIGHT
            MOUSE_BUTTON_RIGHT -> MOUSE_BUTTON_LEFT
            else -> button
        }
    }

    /** Returns the action id for wheel up/down, or null when wheel mapping is disabled. */
    fun mouseWheelActions(mode: Int): Pair<String?, String?> = when (mode) {
        MOUSE_WHEEL_DPAD -> "dpad_up" to "dpad_down"
        MOUSE_WHEEL_TRIGGERS -> "r2" to "l2"
        MOUSE_WHEEL_LEFT_STICK -> "left_stick_up" to "left_stick_down"
        MOUSE_WHEEL_RIGHT_STICK -> "right_stick_up" to "right_stick_down"
        MOUSE_WHEEL_SHOULDERS -> "r1" to "l1"
        else -> null to null
    }

    fun mouseButtonLabel(button: Int): String = when (button) {
        MOUSE_BUTTON_LEFT -> "Left click"
        MOUSE_BUTTON_RIGHT -> "Right click"
        MOUSE_BUTTON_MIDDLE -> "Middle click"
        MOUSE_BUTTON_BACK -> "Back button"
        MOUSE_BUTTON_FORWARD -> "Forward button"
        else -> "Mouse $button"
    }

    private val keyLabels: Map<Int, String> = mapOf(
        KeyEvent.KEYCODE_A to "A", KeyEvent.KEYCODE_B to "B", KeyEvent.KEYCODE_C to "C",
        KeyEvent.KEYCODE_D to "D", KeyEvent.KEYCODE_E to "E", KeyEvent.KEYCODE_F to "F",
        KeyEvent.KEYCODE_G to "G", KeyEvent.KEYCODE_H to "H", KeyEvent.KEYCODE_I to "I",
        KeyEvent.KEYCODE_J to "J", KeyEvent.KEYCODE_K to "K", KeyEvent.KEYCODE_L to "L",
        KeyEvent.KEYCODE_M to "M", KeyEvent.KEYCODE_N to "N", KeyEvent.KEYCODE_O to "O",
        KeyEvent.KEYCODE_P to "P", KeyEvent.KEYCODE_Q to "Q", KeyEvent.KEYCODE_R to "R",
        KeyEvent.KEYCODE_S to "S", KeyEvent.KEYCODE_T to "T", KeyEvent.KEYCODE_U to "U",
        KeyEvent.KEYCODE_V to "V", KeyEvent.KEYCODE_W to "W", KeyEvent.KEYCODE_X to "X",
        KeyEvent.KEYCODE_Y to "Y", KeyEvent.KEYCODE_Z to "Z",
        KeyEvent.KEYCODE_0 to "0", KeyEvent.KEYCODE_1 to "1", KeyEvent.KEYCODE_2 to "2",
        KeyEvent.KEYCODE_3 to "3", KeyEvent.KEYCODE_4 to "4", KeyEvent.KEYCODE_5 to "5",
        KeyEvent.KEYCODE_6 to "6", KeyEvent.KEYCODE_7 to "7", KeyEvent.KEYCODE_8 to "8",
        KeyEvent.KEYCODE_9 to "9"
    )

    fun keyboardKeyLabel(keyCode: Int): String = keyLabels[keyCode] ?: when (keyCode) {
        KeyEvent.KEYCODE_DPAD_UP -> "Up"
        KeyEvent.KEYCODE_DPAD_DOWN -> "Down"
        KeyEvent.KEYCODE_DPAD_LEFT -> "Left"
        KeyEvent.KEYCODE_DPAD_RIGHT -> "Right"
        KeyEvent.KEYCODE_ENTER -> "Enter"
        KeyEvent.KEYCODE_DEL -> "Backspace"
        KeyEvent.KEYCODE_SPACE -> "Space"
        KeyEvent.KEYCODE_TAB -> "Tab"
        KeyEvent.KEYCODE_ESCAPE -> "Esc"
        KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT -> "Shift"
        KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.KEYCODE_CTRL_RIGHT -> "Ctrl"
        KeyEvent.KEYCODE_ALT_LEFT, KeyEvent.KEYCODE_ALT_RIGHT -> "Alt"
        else -> {
            val raw = runCatching { KeyEvent.keyCodeToString(keyCode) }
                .getOrDefault("Key $keyCode")
            raw.removePrefix("KEYCODE_").replace('_', ' ')
        }
    }
}
