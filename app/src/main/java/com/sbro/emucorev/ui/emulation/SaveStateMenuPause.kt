package com.sbro.emucorev.ui.emulation

import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext

internal suspend fun <T> withSaveStateMenuPause(
    setPaused: suspend (Boolean) -> Unit,
    pausedAfter: () -> Boolean,
    operation: suspend () -> T
): T {
    return try {
        setPaused(true)
        operation()
    } finally {
        withContext(NonCancellable) { setPaused(pausedAfter()) }
    }
}
