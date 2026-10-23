package com.sbro.emucorev.ui.common

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.LruCache
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import kotlinx.coroutines.withContext

internal class LocalBitmapLoader(private val decode: (String) -> Bitmap? = ::decodeLocalBitmap) {
    private data class ImageKey(val path: String, val version: Long)
    private val cache = object : LruCache<ImageKey, Bitmap>(16 * 1024 * 1024) {
        override fun sizeOf(key: ImageKey, value: Bitmap): Int = value.allocationByteCount
    }
    private val decoders = Semaphore(4)
    @Volatile private var version = 0L

    fun cached(path: String?, artworkVersion: Long = version): Bitmap? =
        path?.let { cache.get(ImageKey(it, artworkVersion)) }

    @Synchronized
    fun invalidate(): Long {
        version++
        cache.evictAll()
        return version
    }

    suspend fun load(path: String?, artworkVersion: Long = version): Bitmap? {
        if (path.isNullOrBlank()) return null
        cached(path, artworkVersion)?.let { return it }
        return withContext(Dispatchers.IO) {
            decoders.withPermit {
                cached(path, artworkVersion) ?: runCatching { decode(path) }.getOrNull()?.also { bitmap ->
                    synchronized(this@LocalBitmapLoader) {
                        if (artworkVersion == version) cache.put(ImageKey(path, artworkVersion), bitmap)
                    }
                }
            }
        }
    }
}

internal fun localBitmapSampleSize(width: Int, height: Int): Int {
    var sample = 1
    while (maxOf(width, height).toLong() > 512L * sample) sample *= 2
    return sample
}

private fun decodeLocalBitmap(path: String): Bitmap? {
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    BitmapFactory.decodeFile(path, bounds)
    if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null
    return BitmapFactory.decodeFile(path, BitmapFactory.Options().apply {
        inSampleSize = localBitmapSampleSize(bounds.outWidth, bounds.outHeight)
    })
}

internal val localBitmapLoader = LocalBitmapLoader()
