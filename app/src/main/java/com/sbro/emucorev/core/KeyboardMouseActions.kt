package com.sbro.emucorev.core

/**
 * Vita controller actions exposed by the keyboard/mouse mapping UI.
 *
 * Digital actions carry a native SceCtrl button mask. Analog directions carry
 * an axis index and signed deflection; the manager merges those into the pad
 * snapshot sent to the native bridge.
 */
data class KeyboardMouseAction(
    val id: String,
    val buttonMask: Int = 0,
    val axisIndex: Int = -1,
    val axisValue: Float = 0f,
    val shortcut: Boolean = false
)

object KeyboardMouseActions {
    const val ACTION_QUICK_SAVE = "quick_save"
    const val ACTION_QUICK_LOAD = "quick_load"

    val all: List<KeyboardMouseAction> = listOf(
        KeyboardMouseAction("triangle", buttonMask = KeyboardMouseBindings.PAD_BUTTON_TRIANGLE),
        KeyboardMouseAction("circle", buttonMask = KeyboardMouseBindings.PAD_BUTTON_CIRCLE),
        KeyboardMouseAction("cross", buttonMask = KeyboardMouseBindings.PAD_BUTTON_CROSS),
        KeyboardMouseAction("square", buttonMask = KeyboardMouseBindings.PAD_BUTTON_SQUARE),
        KeyboardMouseAction("dpad_up", buttonMask = KeyboardMouseBindings.PAD_BUTTON_UP),
        KeyboardMouseAction("dpad_down", buttonMask = KeyboardMouseBindings.PAD_BUTTON_DOWN),
        KeyboardMouseAction("dpad_left", buttonMask = KeyboardMouseBindings.PAD_BUTTON_LEFT),
        KeyboardMouseAction("dpad_right", buttonMask = KeyboardMouseBindings.PAD_BUTTON_RIGHT),
        KeyboardMouseAction(
            "left_stick_up",
            axisIndex = 1,
            axisValue = -1f
        ),
        KeyboardMouseAction(
            "left_stick_down",
            axisIndex = 1,
            axisValue = 1f
        ),
        KeyboardMouseAction(
            "left_stick_left",
            axisIndex = 0,
            axisValue = -1f
        ),
        KeyboardMouseAction(
            "left_stick_right",
            axisIndex = 0,
            axisValue = 1f
        ),
        KeyboardMouseAction(
            "right_stick_up",
            axisIndex = 3,
            axisValue = -1f
        ),
        KeyboardMouseAction(
            "right_stick_down",
            axisIndex = 3,
            axisValue = 1f
        ),
        KeyboardMouseAction(
            "right_stick_left",
            axisIndex = 2,
            axisValue = -1f
        ),
        KeyboardMouseAction(
            "right_stick_right",
            axisIndex = 2,
            axisValue = 1f
        ),
        KeyboardMouseAction("l1", buttonMask = KeyboardMouseBindings.PAD_BUTTON_L1),
        KeyboardMouseAction("r1", buttonMask = KeyboardMouseBindings.PAD_BUTTON_R1),
        KeyboardMouseAction("l2", buttonMask = KeyboardMouseBindings.PAD_BUTTON_L2),
        KeyboardMouseAction("r2", buttonMask = KeyboardMouseBindings.PAD_BUTTON_R2),
        KeyboardMouseAction("l3", buttonMask = KeyboardMouseBindings.PAD_BUTTON_L3),
        KeyboardMouseAction("r3", buttonMask = KeyboardMouseBindings.PAD_BUTTON_R3),
        KeyboardMouseAction("select", buttonMask = KeyboardMouseBindings.PAD_BUTTON_SELECT),
        KeyboardMouseAction("start", buttonMask = KeyboardMouseBindings.PAD_BUTTON_START),
        KeyboardMouseAction(ACTION_QUICK_SAVE, shortcut = true),
        KeyboardMouseAction(ACTION_QUICK_LOAD, shortcut = true)
    )

    val byId: Map<String, KeyboardMouseAction> = all.associateBy { it.id }
}
