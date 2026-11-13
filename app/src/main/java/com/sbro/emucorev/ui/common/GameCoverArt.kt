package com.sbro.emucorev.ui.common

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import android.util.LruCache
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.data.CoverArtRepository
import com.sbro.emucorev.data.BoundedCoverInputStream
import com.sbro.emucorev.data.CoverDiskCache
import com.sbro.emucorev.data.CoverImagePolicy
import com.sbro.emucorev.data.CoverImageCacheGeneration
import com.sbro.emucorev.data.ResolvedVitaCover
import com.sbro.emucorev.data.VitaCoverResolver
import com.sbro.emucorev.data.VitaCoverStyle
import java.io.InputStream
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope

const val GameCoverAspectRatio: Float = 2f / 3f

private val coverLoads = CoverLoadCoordinator()
private val bitmapGeneration = CoverImageCacheGeneration()
private data class DisplayedGameCover(val bitmap: Bitmap, val is3d: Boolean = false)
private val bitmapCache = object : LruCache<String, Bitmap>(CoverImagePolicy.MEMORY_CACHE_BYTES) {
    override fun sizeOf(key: String, value: Bitmap): Int = value.allocationByteCount
}
/** Whole managed or explicitly chosen artwork on one portrait canvas. */
@Composable
fun GameCoverArt(
    titleId: String? = null,
    igdbId: Long? = null,
    coverPath: String? = null,
    fallbackIconPath: String? = null,
    fallbackTitle: String,
    modifier: Modifier = Modifier,
    styleOverride: VitaCoverStyle? = null,
    contentDescription: String? = fallbackTitle,
    artworkVersion: Long = 0,
    onPresentationChanged: ((is3d: Boolean) -> Unit)? = null
) {
    val app = LocalContext.current.applicationContext
    val repository = remember(app) { CoverArtRepository.get(app) }
    val sharedStyle by repository.style.collectAsState()
    val cacheVersion by repository.cacheVersion.collectAsState()
    val memoryVersion by bitmapGeneration.version.collectAsState()
    val style = styleOverride ?: sharedStyle
    BoxWithConstraints(modifier.aspectRatio(GameCoverAspectRatio)) {
    val decodeWidth = CoverImagePolicy.decodeWidthForCanvas(constraints.maxWidth)
    val decodeHeight = decodeWidth * 3 / 2
    var artwork by remember(titleId, igdbId, coverPath, fallbackIconPath, style, artworkVersion, cacheVersion, memoryVersion, decodeWidth) {
        val resolved = repository.peekResolvedCover(titleId, igdbId, coverPath, fallbackIconPath, style)
        val bitmap = resolved?.path?.let { path ->
            bitmapGeneration.getIfCurrent(memoryVersion) {
                bitmapCache.get(bitmapKey(path, artworkVersion, style, cacheVersion, decodeWidth, decodeHeight))
            }
        }
        mutableStateOf(bitmap?.let { DisplayedGameCover(it, resolved?.is3d == true) })
    }
    val presentationChanged by rememberUpdatedState(onPresentationChanged)
    val is3d = artwork?.is3d == true
    // Callback identity never becomes a key for network or decode work.
    LaunchedEffect(is3d) { presentationChanged?.invoke(is3d) }

    LaunchedEffect(titleId, igdbId, coverPath, fallbackIconPath, style, artworkVersion, cacheVersion, memoryVersion, decodeWidth) {
        if (artwork != null) return@LaunchedEffect
        fun isCurrent() = repository.cacheVersion.value == cacheVersion && bitmapGeneration.version.value == memoryVersion
        coverLoads.loadVisible(
            localCustom = {
                val candidate = coverPath?.takeIf {
                    CoverLoadCoordinator.isLocalCustomCandidate(it)
                }
                if (candidate == null) false else {
                    // Probe only local candidates, with no serial/IGDB identity: this cannot
                    // start a managed download and retains canonical icon/cache exclusions.
                    val local = repository.resolveCover(candidatePath = candidate,
                        fallbackIconPath = fallbackIconPath, styleOverride = style)
                    val customPath = local.path?.takeIf { it != fallbackIconPath }
                    val loaded = customPath?.let {
                        withContext(Dispatchers.IO) { cachedDecode(app, it, artworkVersion, style, cacheVersion, memoryVersion, decodeWidth, decodeHeight) }
                    }
                    currentCoroutineContext().ensureActive()
                    if (loaded == null || !isCurrent()) false else {
                        artwork = DisplayedGameCover(loaded)
                        true
                    }
                }
            },
            cachedArtwork = {
                val resolved = repository.resolveCover(titleId, igdbId, coverPath, fallbackIconPath, style, cachedOnly = true)
                val loaded = withContext(Dispatchers.IO) {
                    resolved.path?.let { cachedDecode(app, it, artworkVersion, style, cacheVersion, memoryVersion, decodeWidth, decodeHeight) }
                }
                currentCoroutineContext().ensureActive()
                if (loaded == null || !isCurrent()) false else {
                    artwork = DisplayedGameCover(loaded, resolved.is3d)
                    true
                }
            },
            managedArtwork = {
                val resolved = repository.resolveCover(titleId, igdbId, coverPath, fallbackIconPath, style)
                val loaded = coverLoads.decode {
                    withContext(Dispatchers.IO) {
                        resolved.path?.let { cachedDecode(app, it, artworkVersion, style, cacheVersion, memoryVersion, decodeWidth, decodeHeight) }
                    }
                }
                currentCoroutineContext().ensureActive()
                if (loaded != null && isCurrent()) artwork = DisplayedGameCover(loaded, resolved.is3d)
            }
        )
    }

    val art = artwork?.bitmap
    val frame = Modifier.fillMaxSize()
    if (art != null) {
        Image(art.asImageBitmap(), contentDescription = contentDescription,
            contentScale = ContentScale.Fit, modifier = frame)
    } else {
        Box(frame.background(MaterialTheme.colorScheme.surfaceVariant), contentAlignment = Alignment.Center) {
            Text(fallbackTitle, style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center,
                maxLines = 4, overflow = TextOverflow.Ellipsis, modifier = Modifier.padding(12.dp))
        }
    }
    }
}

/** Prepare verified disk hits before publishing the library; never starts a download. */
suspend fun warmCachedGameCoverArt(context: Context, titleIds: List<String>, artworkVersion: Long) = coroutineScope {
    val app = context.applicationContext
    val repository = CoverArtRepository.get(app)
    val style = repository.style.value
    val cacheVersion = repository.cacheVersion.value
    val memoryVersion = bitmapGeneration.version.value
    titleIds.distinct().take(48).map { titleId -> async(Dispatchers.IO) {
        coverLoads.decode {
            val result = repository.resolveCover(titleId = titleId, styleOverride = style, cachedOnly = true)
            result.path?.let { cachedDecode(app, it, artworkVersion, style, cacheVersion, memoryVersion) }
        }
    } }.awaitAll()
    Unit
}

/** Speculation is optional: it never queues ahead of an already waiting visible load. */
suspend fun prefetchGameCoverArt(
    context: Context,
    titleId: String? = null,
    igdbId: Long? = null,
    coverPath: String? = null,
    fallbackIconPath: String? = null,
    styleOverride: VitaCoverStyle? = null,
    artworkVersion: Long = 0
) {
    val app = context.applicationContext
    coverLoads.prefetch {
        val repository = CoverArtRepository.get(app)
        val style = styleOverride ?: repository.style.value
        val cacheVersion = repository.cacheVersion.value
        val memoryVersion = bitmapGeneration.version.value
        val result: ResolvedVitaCover = repository.resolveCover(titleId, igdbId, coverPath, fallbackIconPath, style)
        coverLoads.decode {
            withContext(Dispatchers.IO) { result.path?.let { cachedDecode(app, it, artworkVersion, style, cacheVersion, memoryVersion) } }
        }
    }
}

fun clearCoverImageMemoryCache() = bitmapGeneration.invalidate { bitmapCache.evictAll() }

fun invalidateCoverImage(path: String?) {
    if (path.isNullOrBlank()) return
    bitmapGeneration.invalidate {
        bitmapCache.snapshot().keys.filter { it.startsWith("$path@") }.forEach(bitmapCache::remove)
    }
}

private suspend fun cachedDecode(app: Context, path: String, version: Long, style: VitaCoverStyle,
                                 cacheVersion: Long, memoryVersion: Long,
                                 decodeWidth: Int = CoverImagePolicy.DECODE_WIDTH,
                                 decodeHeight: Int = CoverImagePolicy.DECODE_HEIGHT): Bitmap? {
    val repository = CoverArtRepository.get(app)
    if (repository.cacheVersion.value != cacheVersion) return null
    val key = bitmapKey(path, version, style, cacheVersion, decodeWidth, decodeHeight)
    bitmapGeneration.getIfCurrent(memoryVersion) { bitmapCache.get(key) }?.let { return it }
    currentCoroutineContext().ensureActive()
    var unpublished: Bitmap? = null
    return try {
        fun open(): InputStream? {
            val stream = if (path.startsWith("content://")) app.contentResolver.openInputStream(Uri.parse(path))
            else VitaCoverResolver.localFile(path)?.takeIf { it.isFile && it.length() in 1..CoverDiskCache.MAX_BYTES }?.inputStream()
            return stream?.let { BoundedCoverInputStream(it) }
        }
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        open()?.use { BitmapFactory.decodeStream(it, null, bounds); Unit } ?: return null
        val sample = CoverImagePolicy.sampleSize(bounds.outWidth, bounds.outHeight, decodeWidth, decodeHeight) ?: return null
        currentCoroutineContext().ensureActive()
        val options = BitmapFactory.Options().apply { inSampleSize = sample; inPreferredConfig = Bitmap.Config.ARGB_8888 }
        val decoded = open()?.use { BitmapFactory.decodeStream(it, null, options) } ?: return null
        unpublished = decoded
        currentCoroutineContext().ensureActive()
        val scale = minOf(1f, decodeWidth.toFloat() / decoded.width, decodeHeight.toFloat() / decoded.height)
        val bitmap = if (scale < 1f) Bitmap.createScaledBitmap(decoded,
            (decoded.width * scale).toInt().coerceAtLeast(1), (decoded.height * scale).toInt().coerceAtLeast(1), true)
            .also { if (it !== decoded) decoded.recycle() } else decoded
        unpublished = bitmap
        if (repository.cacheVersion.value != cacheVersion) return null
        if (!bitmapGeneration.putIfCurrent(memoryVersion) { bitmapCache.put(key, bitmap) }) return null
        unpublished = null // Published bitmaps may already be displayed by another card.
        bitmap
    } catch (cancelled: CancellationException) {
        throw cancelled
    } catch (_: Exception) {
        null
    } finally {
        unpublished?.recycle()
    }
}

private fun bitmapKey(path: String, version: Long, style: VitaCoverStyle, cacheVersion: Long,
                      width: Int, height: Int) = "$path@$version:${style.name}:${width}x$height:r$cacheVersion"
