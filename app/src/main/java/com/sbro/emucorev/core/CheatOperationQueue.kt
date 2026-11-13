package com.sbro.emucorev.core

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext

/** All blocking pack commits and overlay index mutations share this queue. */
internal object CheatOperationQueue {
    val dispatcher = Dispatchers.IO.limitedParallelism(1)

    suspend fun <T> mutate(
        expectedFingerprint: String,
        currentFingerprint: () -> String,
        action: () -> T
    ): T? = withContext(dispatcher) {
        coroutineContext.ensureActive()
        if (expectedFingerprint != currentFingerprint()) return@withContext null
        action()
    }
}
