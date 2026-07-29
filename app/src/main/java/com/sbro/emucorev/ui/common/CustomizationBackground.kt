package com.sbro.emucorev.ui.common

import android.graphics.drawable.Animatable
import android.media.MediaPlayer
import android.net.Uri
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.VideoView
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import java.io.File

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

@Composable
private fun ImageBackground(file: File, modifier: Modifier) {
    AndroidView(
        modifier = modifier,
        factory = { context ->
            ImageView(context).apply {
                layoutParams = ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT
                )
                scaleType = ImageView.ScaleType.CENTER_CROP
            }
        },
        update = { image ->
            if (image.tag != file.absolutePath) {
                (image.drawable as? Animatable)?.stop()
                image.setImageURI(Uri.fromFile(file))
                image.tag = file.absolutePath
                (image.drawable as? Animatable)?.start()
            }
        }
    )
}

@Composable
private fun VideoBackground(file: File, modifier: Modifier) {
    val lifecycleOwner = LocalLifecycleOwner.current
    val videoViewHolder = remember(file.absolutePath) { arrayOfNulls<VideoView>(1) }
    AndroidView(
        modifier = modifier,
        factory = { context ->
            VideoView(context).also { view ->
                videoViewHolder[0] = view
                view.setOnPreparedListener { player: MediaPlayer ->
                    player.isLooping = true
                    player.setVolume(0f, 0f)
                    applyCenterCrop(view, player.videoWidth, player.videoHeight)
                    view.start()
                }
                view.setOnErrorListener { _, _, _ -> true }
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
