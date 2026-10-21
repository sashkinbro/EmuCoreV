package com.sbro.emucorev.data

/** Global keyboard/mouse configuration. Kept out of the near-limit settings data classes. */
data class KeyboardMouseSettings(
    val keyboardEnabled: Boolean = true,
    val mouseEnabled: Boolean = true,
    val keyboardTargetPad: Int = 0,
    val mouseTargetPad: Int = 0,
    val keyboardBindingsByPad: Map<Int, Map<String, Int>> = emptyMap(),
    val mouseBindingsByPad: Map<Int, Map<String, Int>> = emptyMap(),
    val mouseAimMode: Int = AppPreferences.MOUSE_AIM_OFF,
    val mouseSensitivity: Int = AppPreferences.DEFAULT_MOUSE_SENSITIVITY,
    val mouseVerticalSensitivity: Int = AppPreferences.DEFAULT_MOUSE_VERTICAL_SENSITIVITY,
    val mouseSmoothing: Int = AppPreferences.DEFAULT_MOUSE_SMOOTHING,
    val mouseAimDeadzone: Int = AppPreferences.DEFAULT_MOUSE_DEADZONE,
    val mouseInvertX: Boolean = false,
    val mouseInvertY: Boolean = false,
    val mouseCaptureCursor: Boolean = true,
    val mouseLeftHanded: Boolean = false,
    val mouseWheelMode: Int = AppPreferences.MOUSE_WHEEL_OFF,
    val mouseWheelPulseMs: Int = AppPreferences.DEFAULT_MOUSE_WHEEL_PULSE_MS
)
