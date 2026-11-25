package com.sbro.emucorev.ui.emulation

import android.view.KeyEvent

/** How a key that reaches the Activity while the pause menu is open is handled. */
internal sealed interface MenuKeyAction {
    /** Delivered to the menu's Compose host as [keyCode]. */
    data class Forward(val keyCode: Int) : MenuKeyAction
    /** The menu's Back (closes the menu or a sub-page), run on key up. */
    data object Back : MenuKeyAction
    data object Ignore : MenuKeyAction
}

/**
 * The menu window stays non-focusable so touch and Back behave as before; controller keys are routed into it by the
 * Activity instead. D-pad moves focus, A / Enter activates, B goes back.
 */
internal fun routeMenuKey(keyCode: Int): MenuKeyAction = when (keyCode) {
    KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT,
    KeyEvent.KEYCODE_DPAD_CENTER -> MenuKeyAction.Forward(keyCode)
    KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> MenuKeyAction.Forward(KeyEvent.KEYCODE_DPAD_CENTER)
    KeyEvent.KEYCODE_BUTTON_B -> MenuKeyAction.Back
    else -> MenuKeyAction.Ignore
}

/** Returns true when the event was consumed by the menu. */
internal fun dispatchMenuKey(event: KeyEvent, toMenu: (KeyEvent) -> Boolean, back: () -> Boolean): Boolean =
    when (val action = routeMenuKey(event.keyCode)) {
        is MenuKeyAction.Forward -> {
            val routed = if (action.keyCode == event.keyCode) event else KeyEvent(
                event.downTime, event.eventTime, event.action, action.keyCode, event.repeatCount,
                event.metaState, event.deviceId, event.scanCode, event.flags, event.source)
            toMenu(routed)
            true // keep menu navigation away from the paused game even when the menu ignores it
        }
        MenuKeyAction.Back -> {
            if (event.action == KeyEvent.ACTION_UP && !event.isCanceled) back()
            true
        }
        MenuKeyAction.Ignore -> false
    }
