package com.sbro.emucorev.ui.emulation

internal suspend fun <T> withSaveStateMenuPause(
    setPaused: (Boolean) -> Unit,
    pausedAfter: () -> Boolean,
    operation: suspend () -> T
): T {
    setPaused(true)
    return try {
        operation()
    } finally {
        setPaused(pausedAfter())
    }
}
