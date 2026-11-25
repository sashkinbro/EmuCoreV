package com.sbro.emucorev.ui.store

import android.media.AudioAttributes
import android.media.MediaPlayer
import java.io.File

/**
 * Plays Store sound-pack previews with one [MediaPlayer] at a time: a looping track, or a queue of
 * short sounds one after another. Main thread only. [onFinished] fires when a queue ends or fails,
 * never after [stop].
 */
internal class StoreSoundPreviewPlayer(private val onFinished: () -> Unit) {
    private var player: MediaPlayer? = null
    private var queue: List<File> = emptyList()
    private var loop = false
    private var volume = 1f

    fun play(files: List<File>, loop: Boolean, volume: Float) {
        stop()
        if (files.isEmpty()) {
            onFinished()
            return
        }
        this.queue = files
        this.loop = loop
        this.volume = volume
        playNext()
    }

    fun stop() {
        queue = emptyList()
        releasePlayer()
    }

    private fun playNext() {
        val file = queue.firstOrNull()
        if (file == null) {
            releasePlayer()
            onFinished()
            return
        }
        queue = queue.drop(1)
        releasePlayer()
        val mediaPlayer = MediaPlayer()
        player = mediaPlayer
        try {
            mediaPlayer.setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build()
            )
            mediaPlayer.setDataSource(file.absolutePath)
            mediaPlayer.isLooping = loop
            mediaPlayer.setVolume(volume, volume)
            mediaPlayer.setOnPreparedListener { if (player === it) it.start() }
            mediaPlayer.setOnCompletionListener { if (player === it) playNext() }
            mediaPlayer.setOnErrorListener { failed, _, _ ->
                if (player === failed) {
                    queue = emptyList()
                    releasePlayer()
                    onFinished()
                }
                true
            }
            mediaPlayer.prepareAsync()
        } catch (error: Exception) {
            queue = emptyList()
            releasePlayer()
            onFinished()
        }
    }

    private fun releasePlayer() {
        val current = player ?: return
        player = null
        current.setOnPreparedListener(null)
        current.setOnCompletionListener(null)
        current.setOnErrorListener(null)
        runCatching { current.stop() }
        runCatching { current.release() }
    }
}
