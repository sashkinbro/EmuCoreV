package com.sbro.emucorev.core

import android.content.Context
import android.content.Intent

/** Release twin of the debug lab launch hooks: every call is a no-op. */
internal object LabSession {
    val active: Boolean get() = false
    val statsOverlayAllowed: Boolean get() = true
    fun isLabIntent(intent: Intent?): Boolean = false
    fun isLabExitIntent(intent: Intent?): Boolean = false
    fun begin(intent: Intent?, restored: Boolean) = Unit
    fun accepted(titleId: String, args: Array<String>) = Unit
    fun onStateFrameReady(context: Context) = Unit
    fun onDestroy() = Unit
}
