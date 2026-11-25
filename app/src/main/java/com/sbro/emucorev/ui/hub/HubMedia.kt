package com.sbro.emucorev.ui.hub

import android.annotation.SuppressLint
import android.app.Activity
import android.content.ContentValues
import android.content.Context
import android.content.ContextWrapper
import android.os.Build
import android.provider.MediaStore
import android.view.ViewGroup
import android.widget.Toast
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.rememberTransformableState
import androidx.compose.foundation.gestures.transformable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.calculateEndPadding
import androidx.compose.foundation.layout.calculateStartPadding
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.union
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Download
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import com.sbro.emucorev.ui.theme.style.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalConfiguration
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.ui.window.DialogWindowProvider
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import coil3.compose.AsyncImage
import coil3.request.ImageRequest
import coil3.request.crossfade
import com.pierfrancescosoffritti.androidyoutubeplayer.core.player.YouTubePlayer
import com.pierfrancescosoffritti.androidyoutubeplayer.core.player.listeners.AbstractYouTubePlayerListener
import com.pierfrancescosoffritti.androidyoutubeplayer.core.player.options.IFramePlayerOptions
import com.pierfrancescosoffritti.androidyoutubeplayer.core.player.views.YouTubePlayerView
import com.sbro.emucorev.R
import com.sbro.emucorev.data.hub.HubRepository
import com.sbro.emucorev.ui.common.appScreenTopPadding
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.util.Locale
import com.sbro.emucorev.ui.theme.neon.neonShape

data class HubImageSpec(
    val url: String,
    val sha256: String? = null,
    val bytes: Long? = null,
    val contentDescription: String? = null,
    val downloadAllowed: Boolean = false,
    val attribution: String? = null,
    val sourceUrl: String? = null,
    val licenseUrl: String? = null
)

@Composable
fun HubImage(
    spec: HubImageSpec?,
    modifier: Modifier = Modifier,
    contentScale: ContentScale = ContentScale.Crop
) {
    if (spec == null) {
        Box(modifier = modifier.background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.45f)))
        return
    }
    val context = LocalContext.current
    val repository = remember(context) { HubRepository.get(context) }
    val model by produceState<Any?>(initialValue = spec.url, spec.url, spec.sha256, spec.bytes) {
        val hash = spec.sha256
        val bytes = spec.bytes
        value = if (hash != null && bytes != null) {
            runCatching { repository.cachedAsset(spec.url, hash, bytes) }.getOrElse { spec.url }
        } else {
            spec.url
        }
    }
    AsyncImage(
        model = ImageRequest.Builder(context)
            .data(model)
            .crossfade(true)
            .build(),
        contentDescription = spec.contentDescription,
        contentScale = contentScale,
        modifier = modifier
    )
}

@Composable
fun HubImageViewer(
    title: String,
    images: List<HubImageSpec>,
    startIndex: Int,
    onDismiss: () -> Unit
) {
    if (images.isEmpty()) return
    val context = LocalContext.current
    val uriHandler = androidx.compose.ui.platform.LocalUriHandler.current
    val repository = remember(context) { HubRepository.get(context) }
    val scope = rememberCoroutineScope()
    val pagerState = rememberPagerState(
        initialPage = startIndex.coerceIn(0, images.lastIndex),
        pageCount = images::size
    )
    val savedMessage = stringResource(R.string.hub_image_saved)
    val failedMessage = stringResource(R.string.hub_image_save_failed)

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)
    ) {
        Box(modifier = Modifier.fillMaxSize().background(Color.Black)) {
            HorizontalPager(state = pagerState, modifier = Modifier.fillMaxSize()) { page ->
                ZoomableHubImage(images[page])
            }
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .align(Alignment.TopCenter)
                    .background(Color.Black.copy(alpha = 0.56f))
                    // Keep the title and buttons clear of the camera cutout and status area even
                    // while the system bars are hidden.
                    .windowInsetsPadding(
                        WindowInsets.displayCutout.union(WindowInsets.systemBars)
                            .only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal)
                    )
                    .padding(horizontal = 12.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = title,
                    modifier = Modifier.weight(1f),
                    style = MaterialTheme.typography.titleMedium,
                    color = Color.White,
                    maxLines = 1
                )
                Text(
                    text = stringResource(R.string.hub_image_counter, pagerState.currentPage + 1, images.size),
                    color = Color.White.copy(alpha = 0.78f),
                    style = MaterialTheme.typography.labelMedium
                )
                val current = images[pagerState.currentPage]
                if (current.downloadAllowed && current.sha256 != null && current.bytes != null) {
                    IconButton(onClick = {
                        scope.launch {
                            val saved = runCatching {
                                val file = repository.cachedAsset(current.url, current.sha256, current.bytes)
                                saveHubImage(context, file, title)
                            }.getOrDefault(false)
                            Toast.makeText(context, if (saved) savedMessage else failedMessage, Toast.LENGTH_SHORT).show()
                        }
                    }) {
                        Icon(Icons.Rounded.Download, contentDescription = stringResource(R.string.hub_save_image), tint = Color.White)
                    }
                }
                IconButton(onClick = onDismiss) {
                    Icon(Icons.Rounded.Close, contentDescription = stringResource(R.string.hub_close), tint = Color.White)
                }
            }
            val current = images[pagerState.currentPage]
            current.attribution?.let { attribution ->
                Text(
                    text = attribution,
                    modifier = Modifier
                        .align(Alignment.BottomCenter)
                        .fillMaxWidth()
                        .background(Color.Black.copy(alpha = 0.62f))
                        .clickable(enabled = current.sourceUrl != null) {
                            current.sourceUrl?.let { runCatching { uriHandler.openUri(it) } }
                        }
                        .windowInsetsPadding(
                            WindowInsets.displayCutout.union(WindowInsets.systemBars)
                                .only(WindowInsetsSides.Bottom + WindowInsetsSides.Horizontal)
                        )
                        // Extra room so the caption never runs into rounded screen corners.
                        .padding(start = 24.dp, end = 24.dp, top = 10.dp, bottom = 18.dp),
                    color = Color.White.copy(alpha = 0.9f),
                    style = MaterialTheme.typography.bodySmall,
                    maxLines = 3
                )
            }
        }
    }
}

@Composable
private fun ZoomableHubImage(spec: HubImageSpec) {
    var scale by remember(spec.url) { mutableFloatStateOf(1f) }
    var offsetX by remember(spec.url) { mutableFloatStateOf(0f) }
    var offsetY by remember(spec.url) { mutableFloatStateOf(0f) }
    val transformState = rememberTransformableState { _, zoomChange, panChange, _ ->
        scale = (scale * zoomChange).coerceIn(1f, 5f)
        if (scale <= 1.01f) {
            offsetX = 0f
            offsetY = 0f
        } else {
            offsetX += panChange.x
            offsetY += panChange.y
        }
    }
    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        HubImage(
            spec = spec,
            modifier = Modifier
                .fillMaxSize()
                .graphicsLayer {
                    scaleX = scale
                    scaleY = scale
                    translationX = offsetX
                    translationY = offsetY
                }
                .transformable(transformState),
            contentScale = ContentScale.Fit
        )
    }
}

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
fun HubVideoPlayer(
    youtubeId: String,
    title: String,
    onDismiss: () -> Unit
) {
    val lifecycleOwner = LocalLifecycleOwner.current
    val context = LocalContext.current
    val configuration = LocalConfiguration.current
    val layoutDirection = LocalLayoutDirection.current
    val activity = remember(context) { context.findActivity() }
    val isLandscape = configuration.screenWidthDp > configuration.screenHeightDp
    val topInset = appScreenTopPadding()
    val cutoutPadding = WindowInsets.displayCutout.asPaddingValues()
    val cutoutStart = cutoutPadding.calculateStartPadding(layoutDirection)
    val cutoutEnd = cutoutPadding.calculateEndPadding(layoutDirection)
    val normalizedYoutubeId = remember(youtubeId) {
        youtubeId.trim().let { value ->
            when {
                value.contains("v=") -> value.substringAfter("v=").substringBefore("&").substringBefore("?")
                value.contains("youtu.be/") -> value.substringAfter("youtu.be/").substringBefore("?").substringBefore("&")
                else -> value.substringBefore("?").substringBefore("&")
            }
        }
    }
    val options = remember {
        IFramePlayerOptions.Builder(context).controls(1).build()
    }
    val view = remember {
        YouTubePlayerView(context).apply {
            enableAutomaticInitialization = false
            layoutParams = ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
            clipChildren = false
            clipToPadding = false
        }
    }
    var player by remember { mutableStateOf<YouTubePlayer?>(null) }
    var initialized by remember { mutableStateOf(false) }
    var loadedVideoId by remember { mutableStateOf<String?>(null) }

    DisposableEffect(lifecycleOwner, view) {
        lifecycleOwner.lifecycle.addObserver(view)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(view)
            view.release()
        }
    }
    DisposableEffect(view) {
        if (!initialized) {
            view.initialize(object : AbstractYouTubePlayerListener() {
                override fun onReady(youTubePlayer: YouTubePlayer) {
                    player = youTubePlayer
                }
            }, options)
            initialized = true
        }
        onDispose { }
    }
    LaunchedEffect(player, normalizedYoutubeId) {
        val readyPlayer = player ?: return@LaunchedEffect
        if (loadedVideoId != normalizedYoutubeId) {
            readyPlayer.loadVideo(normalizedYoutubeId, 0f)
            loadedVideoId = normalizedYoutubeId
        }
    }

    DisposableEffect(activity) {
        val window = activity?.window
        val controller = window?.let { WindowInsetsControllerCompat(it, it.decorView) }
        controller?.hide(WindowInsetsCompat.Type.systemBars())
        controller?.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        onDispose {
            controller?.show(WindowInsetsCompat.Type.navigationBars())
            controller?.hide(WindowInsetsCompat.Type.statusBars())
        }
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)
    ) {
        val dialogView = LocalView.current
        val dialogWindow = (dialogView.parent as? DialogWindowProvider)?.window
        DisposableEffect(dialogWindow) {
            if (dialogWindow != null) {
                WindowCompat.setDecorFitsSystemWindows(dialogWindow, false)
                dialogWindow.setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
                val controller = WindowInsetsControllerCompat(dialogWindow, dialogWindow.decorView)
                controller.hide(WindowInsetsCompat.Type.systemBars())
                controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
            onDispose { }
        }

        Box(modifier = Modifier.fillMaxSize().background(Color.Black)) {
            BoxWithConstraints(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                val videoRatio = 16f / 9f
                val screenRatio = maxWidth / maxHeight
                val videoWidth = if (screenRatio > videoRatio) maxHeight * videoRatio else maxWidth
                val videoHeight = if (screenRatio > videoRatio) maxHeight else maxWidth / videoRatio
                AndroidView(
                    factory = { view },
                    modifier = Modifier.size(width = videoWidth, height = videoHeight)
                )
            }
            if (isLandscape) {
                Surface(
                    modifier = Modifier
                        .align(Alignment.TopEnd)
                        .padding(
                            top = topInset + 8.dp,
                            end = cutoutEnd + 16.dp
                        ),
                    shape = androidx.compose.foundation.shape.CircleShape,
                    color = Color.Black.copy(alpha = 0.48f),
                    onClick = onDismiss
                ) {
                    Box(modifier = Modifier.size(42.dp), contentAlignment = Alignment.Center) {
                        Icon(Icons.Rounded.Close, contentDescription = stringResource(R.string.hub_close), tint = Color.White)
                    }
                }
            } else {
                Surface(
                    modifier = Modifier
                        .align(Alignment.TopCenter)
                        .padding(
                            start = cutoutStart + 12.dp,
                            top = topInset + 8.dp,
                            end = cutoutEnd + 12.dp
                        )
                        .fillMaxWidth(),
                    shape = neonShape(18.dp),
                    color = Color.Black.copy(alpha = 0.72f)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(title, modifier = Modifier.weight(1f), color = Color.White, maxLines = 1)
                        IconButton(onClick = onDismiss) {
                            Icon(Icons.Rounded.Close, contentDescription = stringResource(R.string.hub_close), tint = Color.White)
                        }
                    }
                }
            }
            if (player == null) CircularProgressIndicator(color = Color.White)
        }
    }
}

private tailrec fun Context.findActivity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.findActivity()
    else -> null
}

private suspend fun saveHubImage(context: Context, source: File, title: String): Boolean = withContext(Dispatchers.IO) {
    val format = detectHubImageFormat(source) ?: return@withContext false
    val safeTitle = title.lowercase(Locale.ROOT)
        .replace(Regex("[^a-z0-9]+"), "_")
        .trim('_')
        .take(48)
        .ifBlank { "hub_image" }
    val values = ContentValues().apply {
        put(MediaStore.Images.Media.DISPLAY_NAME, "emucore_${safeTitle}_${System.currentTimeMillis()}.${format.extension}")
        put(MediaStore.Images.Media.MIME_TYPE, format.mimeType)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            put(MediaStore.Images.Media.RELATIVE_PATH, "Pictures/EmuCore")
            put(MediaStore.Images.Media.IS_PENDING, 1)
        }
    }
    val resolver = context.contentResolver
    val uri = resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values) ?: return@withContext false
    val saved = runCatching {
        resolver.openOutputStream(uri, "w")?.use { output -> source.inputStream().use { it.copyTo(output) } }
        true
    }.getOrDefault(false)
    if (saved && Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        resolver.update(uri, ContentValues().apply { put(MediaStore.Images.Media.IS_PENDING, 0) }, null, null)
    } else if (!saved) {
        resolver.delete(uri, null, null)
    }
    saved
}

private data class HubImageFormat(val extension: String, val mimeType: String)

private fun detectHubImageFormat(file: File): HubImageFormat? {
    val header = ByteArray(12)
    val read = file.inputStream().buffered().use { it.read(header) }
    if (read >= 8 && header.take(8).map(Byte::toInt) == listOf(0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a)) {
        return HubImageFormat("png", "image/png")
    }
    if (read >= 3 && header[0].toInt() and 0xff == 0xff && header[1].toInt() and 0xff == 0xd8 && header[2].toInt() and 0xff == 0xff) {
        return HubImageFormat("jpg", "image/jpeg")
    }
    if (read >= 12 && header.copyOfRange(0, 4).toString(Charsets.US_ASCII) == "RIFF" &&
        header.copyOfRange(8, 12).toString(Charsets.US_ASCII) == "WEBP"
    ) {
        return HubImageFormat("webp", "image/webp")
    }
    return null
}
