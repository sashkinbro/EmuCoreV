package com.sbro.emucorev.core

import android.content.Context
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.media.RingtoneManager
import android.net.Uri
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import com.sbro.emucorev.data.TrophySoundMode
import com.sbro.emucorev.data.TrophySoundPlaybackRoute
import com.sbro.emucorev.data.TrophySoundSettings
import com.sbro.emucorev.data.resolveTrophySoundPlayback
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

internal interface TrophySoundBackend {
    fun prepare(uri: Uri, onPrepared: () -> Unit, onCompletion: () -> Unit, onError: () -> Unit)
    fun start()
    fun close()
}

internal fun interface TrophySoundBackendFactory {
    fun create(context: Context): TrophySoundBackend
}

/** All MediaPlayer operations run on a private looper so document providers cannot block the UI/game thread. */
class TrophySoundPlayer internal constructor(
    context: Context,
    private val onCustomSoundFailure: (Uri) -> Unit = {},
    private val backendFactory: TrophySoundBackendFactory = AndroidTrophySoundBackendFactory,
    private val systemUriProvider: () -> Uri? = {
        RingtoneManager.getDefaultUri(RingtoneManager.TYPE_NOTIFICATION)
    }
) : AutoCloseable {
    private val appContext = context.applicationContext
    private val worker = HandlerThread("TrophySoundPlayer").apply { start() }
    private val handler = Handler(worker.looper)
    private val requestId = AtomicInteger()
    private val closed = AtomicBoolean(false)
    // Accessed only from worker's looper.
    private var active: ActiveBackend? = null

    fun play(settings: TrophySoundSettings) {
        if (closed.get()) return
        val request = requestId.incrementAndGet()
        val route = resolveTrophySoundPlayback(settings)
        handler.post {
            if (isCurrent(request)) handleRoute(route, request)
        }
    }

    /** Stops a settings preview while keeping this instance reusable. */
    fun stop() {
        if (closed.get()) return
        val stopRequest = requestId.incrementAndGet()
        handler.post {
            if (isCurrent(stopRequest)) releaseCurrent()
        }
    }

    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        requestId.incrementAndGet()
        handler.post {
            releaseCurrent()
            worker.quitSafely()
        }
    }

    private fun handleRoute(route: TrophySoundPlaybackRoute, request: Int) {
        when (route) {
            TrophySoundPlaybackRoute.Silent -> releaseCurrent()
            TrophySoundPlaybackRoute.SystemNotification -> playUri(systemUriProvider(), null, request)
            is TrophySoundPlaybackRoute.Custom -> playUri(route.uri, route.uri, request)
        }
    }

    private fun playUri(uri: Uri?, customUri: Uri?, request: Int) {
        releaseCurrent()
        if (!isCurrent(request) || uri == null) return
        val backend = try {
            backendFactory.create(appContext)
        } catch (error: Exception) {
            Log.w(TAG, "Could not create trophy sound player", error)
            if (customUri != null) failCustom(customUri, request)
            return
        }
        val activeBackend = ActiveBackend(request, backend, customUri)
        active = activeBackend
        try {
            backend.prepare(
                uri = uri,
                onPrepared = {
                    handler.post {
                        if (isActive(activeBackend)) {
                            runCatching { backend.start() }.onFailure { error ->
                                Log.w(TAG, "Could not start trophy sound", error)
                                failBackend(activeBackend)
                            }
                        } else {
                            closeBackend(backend)
                        }
                    }
                },
                onCompletion = {
                    handler.post {
                        if (isActive(activeBackend)) releaseCurrent()
                        else closeBackend(backend)
                    }
                },
                onError = {
                    handler.post { failBackend(activeBackend) }
                }
            )
        } catch (error: Exception) {
            Log.w(TAG, "Could not prepare trophy sound", error)
            failBackend(activeBackend)
        }
    }

    private fun failBackend(failed: ActiveBackend) {
        if (!isActive(failed)) {
            closeBackend(failed.backend)
            return
        }
        active = null
        closeBackend(failed.backend)
        failed.customUri?.let { customUri ->
            failCustom(customUri, failed.request)
        }
    }

    private fun failCustom(uri: Uri, request: Int) {
        if (!isCurrent(request)) return
        runCatching { onCustomSoundFailure(uri) }
            .onFailure { Log.w(TAG, "Could not mark custom trophy sound unavailable", it) }
        // This call uses the same request id and a null custom URI, so fallback cannot recurse.
        playUri(systemUriProvider(), null, request)
    }

    private fun isActive(candidate: ActiveBackend): Boolean =
        isCurrent(candidate.request) && active === candidate

    private fun isCurrent(request: Int): Boolean = !closed.get() && requestId.get() == request

    private fun releaseCurrent() {
        val current = active ?: return
        active = null
        closeBackend(current.backend)
    }

    private fun closeBackend(backend: TrophySoundBackend) {
        runCatching { backend.close() }
            .onFailure { Log.w(TAG, "Could not release trophy sound player", it) }
    }

    private data class ActiveBackend(
        val request: Int,
        val backend: TrophySoundBackend,
        val customUri: Uri?
    )

    private companion object {
        const val TAG = "TrophySoundPlayer"
    }
}

private object AndroidTrophySoundBackendFactory : TrophySoundBackendFactory {
    override fun create(context: Context): TrophySoundBackend = AndroidTrophySoundBackend(context)
}

private class AndroidTrophySoundBackend(private val context: Context) : TrophySoundBackend {
    private val player = MediaPlayer()
    private var released = false

    override fun prepare(uri: Uri, onPrepared: () -> Unit, onCompletion: () -> Unit, onError: () -> Unit) {
        player.setAudioAttributes(
            AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .build()
        )
        player.setDataSource(context, uri)
        player.setOnPreparedListener { onPrepared() }
        player.setOnCompletionListener { onCompletion() }
        player.setOnErrorListener { _, _, _ ->
            onError()
            true
        }
        player.prepareAsync()
    }

    override fun start() = player.start()

    override fun close() {
        if (released) return
        released = true
        runCatching { player.setOnPreparedListener(null) }
        runCatching { player.setOnCompletionListener(null) }
        runCatching { player.setOnErrorListener(null) }
        runCatching { player.reset() }
        runCatching { player.release() }
    }
}
