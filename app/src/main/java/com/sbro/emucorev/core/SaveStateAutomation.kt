package com.sbro.emucorev.core

import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext

internal const val AUTOMATIC_STATE_LOAD_EXTRA = "AutomaticStateLoad"
internal const val SKIP_AUTOMATIC_STATE_LOAD_EXTRA = "SkipAutomaticStateLoad"

internal fun resolveAutoLoadArguments(args: Array<String>, enabled: Boolean, skipAutomatic: Boolean = false,
                                     validQuickPath: () -> String?): Array<String> {
    if (!enabled || skipAutomatic || args.contains("-loadstate")) return args
    val titleIndex = args.indexOf("-r") + 1
    if (titleIndex <= 0 || args.getOrNull(titleIndex).isNullOrBlank()) return args
    val path = validQuickPath()?.takeIf { it.isNotBlank() } ?: return args
    return args + arrayOf("-loadstate", path)
}

internal fun autoLoadFallbackArguments(args: Array<out String>): Array<String> {
    val fallback = mutableListOf<String>()
    var index = 0
    while (index < args.size) {
        if (args[index] == "-loadstate") index += 2 else fallback += args[index++]
    }
    return fallback.toTypedArray()
}

internal fun shouldSkipExitAutoSave(args: Array<String>?, automaticLoad: Boolean): Boolean =
    !automaticLoad && args?.contains("-loadstate") == true

/** First controlled stop owns both the outgoing title and the shutdown destination. */
internal class SaveStateExitCoordinator {
    private val claimed = AtomicBoolean(false)

    suspend fun run(titleId: String, saveEnabled: Boolean, save: suspend (String) -> Unit, shutdown: () -> Unit) {
        if (!claimed.compareAndSet(false, true)) return
        withContext(NonCancellable) {
            try {
                if (saveEnabled && titleId.isNotBlank()) save(titleId)
            } finally {
                shutdown()
            }
        }
    }
}
