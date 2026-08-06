package com.sbro.emucorev.ui.common

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.drawable.Animatable
import android.media.MediaPlayer
import android.util.Log
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.VideoView
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.sbro.emucorev.data.CustomizationPreferences
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

private const val TAG = "CustomizationBackground"

@Composable
fun CustomizationBackground(
    path: String?,
    mimeType: String?,
    modifier: Modifier = Modifier
) {
    val file = remember(path) { path?.let(::File)?.takeIf(File::isFile) } ?: return
    if (mimeType?.startsWith("video/") == true) {
        VideoBackground(file, modifier)
    } else {
        ImageBackground(file, modifier)
    }
}

/**
 * Drops a background that cannot be rendered.
 *
 * Without this the same unusable file is re-read on every launch, and because
 * the background is composed on the startup screen a decode failure turns into
 * a permanent crash loop that only a full data wipe clears.
 */
private fun discardUnusableBackground(context: Context, file: File, reason: String) {
    Log.w(TAG, "Discarding unusable background '${file.absolutePath}': $reason")
    runCatching {
        CustomizationPreferences(context.applicationContext).use { it.clearBackground() }
        file.delete()
    }.onFailure { error ->
        Log.w(TAG, "Failed to discard background", error)
    }
}

@Composable
private fun ImageBackground(file: File, modifier: Modifier) {
    val context = LocalContext.current
    var bitmap by remember(file.absolutePath) { mutableStateOf<Bitmap?>(null) }

    LaunchedEffect(file.absolutePath) {
        val decoded = withContext(Dispatchers.IO) {
            decodeSampledBitmap(file)
        }
        if (decoded == null) {
            discardUnusableBackground(context, file, "decode failed")
        }
        bitmap = decoded
    }

    val currentBitmap = bitmap ?: return

    AndroidView(
        modifier = modifier,
        factory = { ctx ->
            ImageView(ctx).apply {
                layoutParams = ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT
                )
                scaleType = ImageView.ScaleType.CENTER_CROP
            }
        },
        update = { image ->
            if (image.tag != currentBitmap) {
                (image.drawable as? Animatable)?.stop()
                image.setImageBitmap(currentBitmap)
                image.tag = currentBitmap
                (image.drawable as? Animatable)?.start()
            }
        }
    )
}

/**
 * Decodes [file] downscaled to at most [MAX_BACKGROUND_PIXELS].
 *
 * A background is only ever shown full-screen, so decoding at full resolution
 * wastes memory and reliably throws [OutOfMemoryError] on large images. That is
 * an [Error] rather than an [Exception], so it must be caught explicitly.
 */
private fun decodeSampledBitmap(file: File): Bitmap? = try {
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    BitmapFactory.decodeFile(file.absolutePath, bounds)

    if (bounds.outWidth <= 0 || bounds.outHeight <= 0) {
        null
    } else {
        var sampleSize = 1
        while (
            (bounds.outWidth.toLong() / sampleSize) * (bounds.outHeight.toLong() / sampleSize) >
            MAX_BACKGROUND_PIXELS
        ) {
            sampleSize *= 2
        }

        BitmapFactory.decodeFile(
            file.absolutePath,
            BitmapFactory.Options().apply {
                inSampleSize = sampleSize
                inPreferredConfig = Bitmap.Config.RGB_565
            }
        )
    }
} catch (error: OutOfMemoryError) {
    Log.w(TAG, "Out of memory decoding background", error)
    null
} catch (error: Exception) {
    Log.w(TAG, "Failed to decode background", error)
    null
}

/** Roughly a 1440p screen; more detail than this is never visible. */
private const val MAX_BACKGROUND_PIXELS = 4_000_000L

@Composable
private fun VideoBackground(file: File, modifier: Modifier) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current
    val videoViewHolder = remember(file.absolutePath) { arrayOfNulls<VideoView>(1) }
    AndroidView(
        modifier = modifier,
        factory = { ctx ->
            VideoView(ctx).also { view ->
                videoViewHolder[0] = view
                view.setOnPreparedListener { player: MediaPlayer ->
                    player.isLooping = true
                    player.setVolume(0f, 0f)
                    applyCenterCrop(view, player.videoWidth, player.videoHeight)
                    view.start()
                }
                view.setOnErrorListener { _, what, extra ->
                    // The player is now in the Error state and will never
                    // recover, so drop the background instead of leaving a
                    // permanently blank screen behind.
                    discardUnusableBackground(context, file, "playback error what=$what extra=$extra")
                    true
                }
                view.setVideoPath(file.absolutePath)
            }
        },
        update = { view ->
            videoViewHolder[0] = view
            if (view.tag != file.absolutePath) {
                view.tag = file.absolutePath
                view.setVideoPath(file.absolutePath)
            }
            if (!view.isPlaying && lifecycleOwner.lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED)) {
                view.start()
            }
        }
    )
    DisposableEffect(lifecycleOwner, file.absolutePath) {
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_START, Lifecycle.Event.ON_RESUME -> videoViewHolder[0]?.start()
                Lifecycle.Event.ON_PAUSE, Lifecycle.Event.ON_STOP -> videoViewHolder[0]?.pause()
                else -> Unit
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            videoViewHolder[0]?.stopPlayback()
            videoViewHolder[0] = null
        }
    }
}

private fun applyCenterCrop(view: VideoView, videoWidth: Int, videoHeight: Int) {
    if (videoWidth <= 0 || videoHeight <= 0) return
    view.post {
        if (view.width <= 0 || view.height <= 0) return@post
        val videoRatio = videoWidth.toFloat() / videoHeight
        val viewRatio = view.width.toFloat() / view.height
        if (videoRatio > viewRatio) {
            view.scaleX = videoRatio / viewRatio
            view.scaleY = 1f
        } else {
            view.scaleX = 1f
            view.scaleY = viewRatio / videoRatio
        }
    }
}
