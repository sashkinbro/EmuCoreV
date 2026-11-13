package com.sbro.emucorev.data

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

internal class CoverImageCacheGeneration {
    private val monitor = Any()
    private var generation = 0L
    private val mutableVersion = MutableStateFlow(0L)
    val version: StateFlow<Long> = mutableVersion.asStateFlow()
    fun <T> getIfCurrent(expectedVersion: Long, read: () -> T): T? = synchronized(monitor) {
        if (expectedVersion == generation) read() else null
    }
    fun putIfCurrent(expectedVersion: Long, write: () -> Unit): Boolean = synchronized(monitor) {
        if (expectedVersion != generation) false else { write(); true }
    }
    fun invalidate(clear: () -> Unit) = synchronized(monitor) {
        generation++
        clear()
        mutableVersion.value = generation
    }
}

internal object CoverImagePolicy {
    const val MEMORY_CACHE_BYTES = 64 * 1024 * 1024
    const val DECODE_WIDTH = 400
    const val DECODE_HEIGHT = 600
    fun decodeWidthForCanvas(width: Int): Int = if (width <= DECODE_WIDTH) DECODE_WIDTH else 720
    fun validBounds(width: Int, height: Int): Boolean = width in 1..16_384 && height in 1..16_384 &&
        width.toLong() * height <= 64L * 1024 * 1024

    fun sampleSize(width: Int, height: Int, requestedWidth: Int = DECODE_WIDTH,
                   requestedHeight: Int = DECODE_HEIGHT): Int? {
        if (!validBounds(width, height)) return null
        if (requestedWidth <= 0 || requestedHeight <= 0) return null
        var sample = 1
        while ((width + sample - 1) / sample > requestedWidth ||
            (height + sample - 1) / sample > requestedHeight) sample *= 2
        return sample
    }
}
