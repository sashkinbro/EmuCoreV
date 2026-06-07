package com.sbro.emucorev.ui.common

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
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
import androidx.compose.ui.platform.LocalContext
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.net.URL

@Composable
fun UrlImage(
    imageUrl: String?,
    contentDescription: String,
    fallbackLabel: String,
    modifier: Modifier = Modifier,
    pinInMemory: Boolean = false
) {
    val context = LocalContext.current
    val bitmap = produceState(
        initialValue = imageUrl?.let(UrlBitmapMemoryCache::get),
        key1 = imageUrl,
        key2 = context,
        key3 = pinInMemory
    ) {
        value = if (imageUrl.isNullOrBlank()) {
            null
        } else {
            UrlBitmapMemoryCache.get(imageUrl)?.let {
                return@produceState
            }
            withContext(Dispatchers.IO) {
                runCatching {
                    if (imageUrl.startsWith("content://") || imageUrl.startsWith("file://")) {
                        context.contentResolver.openInputStream(Uri.parse(imageUrl))?.use(BitmapFactory::decodeStream)
                    } else {
                        URL(imageUrl).openStream().use(BitmapFactory::decodeStream)
                    }
                }.getOrNull()?.also { bitmap ->
                    UrlBitmapMemoryCache.put(imageUrl, bitmap, pinned = pinInMemory)
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
    private val pinnedCache = mutableMapOf<String, Bitmap>()
    private val cache = object : LruCache<String, Bitmap>(calculateCacheSizeKb()) {
        override fun sizeOf(key: String, value: Bitmap): Int {
            return (value.byteCount / 1024).coerceAtLeast(1)
        }
    }

    @Synchronized
    fun get(url: String): Bitmap? = pinnedCache[url] ?: cache.get(url)

    @Synchronized
    fun put(url: String, bitmap: Bitmap, pinned: Boolean = false) {
        if (pinned) {
            pinnedCache[url] = bitmap
            return
        }
        if (pinnedCache[url] == null && cache.get(url) == null) {
            cache.put(url, bitmap)
        }
    }

    private fun calculateCacheSizeKb(): Int {
        val maxMemoryKb = (Runtime.getRuntime().maxMemory() / 1024L).toInt()
        return (maxMemoryKb / 8).coerceAtLeast(8 * 1024)
    }
}
