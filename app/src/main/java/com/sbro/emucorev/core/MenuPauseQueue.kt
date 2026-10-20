package com.sbro.emucorev.core

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.launch

/** Native pause takes kernel/audio locks. Keep it ordered and off Android's input thread. */
internal class MenuPauseQueue(scope: CoroutineScope, applyPause: (Boolean) -> Unit) {
    private data class Request(val paused: Boolean, val completion: CompletableDeferred<Unit>)
    private val requests = Channel<Request>(Channel.UNLIMITED)

    init {
        scope.launch(Dispatchers.IO) {
            for (request in requests) {
                try {
                    applyPause(request.paused)
                    request.completion.complete(Unit)
                } catch (error: Exception) {
                    request.completion.completeExceptionally(error)
                }
            }
        }
    }

    fun request(paused: Boolean): Deferred<Unit> {
        val completion = CompletableDeferred<Unit>()
        val sent = requests.trySend(Request(paused, completion))
        if (sent.isFailure) completion.completeExceptionally(IllegalStateException("Menu pause queue is closed"))
        return completion
    }

    fun close() { requests.close() }
}
