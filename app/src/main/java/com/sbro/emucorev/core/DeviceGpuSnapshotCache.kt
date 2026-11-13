package com.sbro.emucorev.core
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** A slow fallback acquisition must never replace a completed hardware probe. */
internal class DeviceGpuSnapshotCache {
    private val monitor = Any()
    @Volatile private var cached: DeviceGpuSnapshot? = null
    private val state = MutableStateFlow<DeviceGpuSnapshot?>(null)
    val snapshot: StateFlow<DeviceGpuSnapshot?> = state.asStateFlow()

    fun peek(): DeviceGpuSnapshot? = cached

    fun getOrResolve(resolve: () -> DeviceGpuSnapshot): DeviceGpuSnapshot {
        cached?.let { return it }
        // Acquisition may do slow I/O; never hold the publication monitor while it runs.
        val candidate = resolve()
        return synchronized(monitor) {
            cached ?: candidate.also {
                cached = it
                state.value = it
            }
        }
    }

    fun publish(value: DeviceGpuSnapshot) = synchronized(monitor) {
        cached = value
        state.value = value
    }
}
