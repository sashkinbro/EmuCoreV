package com.sbro.emucorev.data

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.catch

/**
 * Like [runCatching], but never swallows a [CancellationException]: a cancelled coroutine must keep
 * unwinding instead of reporting a bogus failure and continuing to run follow-up work (B072).
 */
inline fun <T> runCatchingCancellable(block: () -> T): Result<T> {
    return try {
        Result.success(block())
    } catch (e: CancellationException) {
        throw e
    } catch (e: Throwable) {
        Result.failure(e)
    }
}

/**
 * Collects [this] and reports an upstream failure (for example a snapshot listener that
 * `close(error)`d) through [onError] instead of ending the collecting coroutine silently (B061).
 * Cancellation is never reported.
 */
suspend fun <T> Flow<T>.collectReporting(onError: (Throwable) -> Unit, onEach: suspend (T) -> Unit) {
    catch { error -> onError(error) }.collect { onEach(it) }
}
