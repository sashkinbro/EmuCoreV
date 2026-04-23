package com.sbro.emucorev.ui.common

import android.os.SystemClock
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember

@Composable
fun rememberDebouncedClick(
    debounceMillis: Long = 550L,
    onClick: () -> Unit
): () -> Unit {
    val gate = remember { ClickGate(debounceMillis) }
    return remember(onClick, debounceMillis) {
        {
            if (gate.tryAcquire()) {
                onClick()
            }
        }
    }
}

private class ClickGate(
    private val debounceMillis: Long
) {
    private var lastClickTimestamp: Long = 0L

    fun tryAcquire(): Boolean {
        val now = SystemClock.elapsedRealtime()
        if (now - lastClickTimestamp < debounceMillis) return false
        lastClickTimestamp = now
        return true
    }
}
