package com.sbro.emucorev.ui.common

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.LruCache
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.produceState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.net.URL

@Composable
fun UrlImage(
    imageUrl: String?,
    contentDescription: String,
    fallbackLabel: String,
    modifier: Modifier = Modifier
) {
    val bitmap = produceState(
        initialValue = imageUrl?.let(UrlBitmapMemoryCache::get),
        key1 = imageUrl
    ) {
        value = if (imageUrl.isNullOrBlank()) {
            null
        } else {
            UrlBitmapMemoryCache.get(imageUrl)?.let {
                return@produceState
            }
            withContext(Dispatchers.IO) {
                runCatching {
                    URL(imageUrl).openStream().use(BitmapFactory::decodeStream)
                }.getOrNull()?.also { bitmap ->
                    UrlBitmapMemoryCache.put(imageUrl, bitmap)
                }
            }
        }
    }.value

    if (bitmap != null) {
        Image(
            bitmap = bitmap.asImageBitmap(),
            contentDescription = contentDescription,
            modifier = modifier,
            contentScale = ContentScale.Crop
        )
    } else {
        Box(
            modifier = modifier.background(MaterialTheme.colorScheme.surfaceVariant),
            contentAlignment = Alignment.Center
        ) {
            Text(
                text = fallbackLabel.take(1).uppercase(),
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

private object UrlBitmapMemoryCache {
    private val cache = object : LruCache<String, Bitmap>(calculateCacheSizeKb()) {
        override fun sizeOf(key: String, value: Bitmap): Int {
            return (value.byteCount / 1024).coerceAtLeast(1)
        }
    }

    fun get(url: String): Bitmap? = cache.get(url)

    fun put(url: String, bitmap: Bitmap) {
        if (cache.get(url) == null) {
            cache.put(url, bitmap)
        }
    }

    private fun calculateCacheSizeKb(): Int {
        val maxMemoryKb = (Runtime.getRuntime().maxMemory() / 1024L).toInt()
        return (maxMemoryKb / 8).coerceAtLeast(8 * 1024)
    }
}
