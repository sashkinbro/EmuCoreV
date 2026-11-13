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
    const val DECODE_WIDTH = 720
    const val DECODE_HEIGHT = 1080
    fun validBounds(width: Int, height: Int): Boolean = width in 1..16_384 && height in 1..16_384 &&
        width.toLong() * height <= 64L * 1024 * 1024

    fun sampleSize(width: Int, height: Int): Int? {
        if (!validBounds(width, height)) return null
        var sample = 1
        while ((width + sample - 1) / sample > DECODE_WIDTH ||
            (height + sample - 1) / sample > DECODE_HEIGHT) sample *= 2
        return sample
    }
}
