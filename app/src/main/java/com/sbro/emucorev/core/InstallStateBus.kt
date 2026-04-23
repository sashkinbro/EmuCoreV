package com.sbro.emucorev.core

import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.asSharedFlow

object InstallStateBus {
    private val _events = MutableSharedFlow<Long>(extraBufferCapacity = 1)
    val events = _events.asSharedFlow()

    fun notifyCompleted() {
        _events.tryEmit(System.currentTimeMillis())
    }
}
