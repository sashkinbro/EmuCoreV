package com.sbro.emucorev.ui.cheats

import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive

/** Once native import starts it completes atomically under the save-operation mutex. */
internal suspend fun <T> commitCheatPackIfCurrent(canCommit: () -> Boolean, import: () -> T): T? {
    currentCoroutineContext().ensureActive()
    if (!canCommit()) return null
    return import()
}
