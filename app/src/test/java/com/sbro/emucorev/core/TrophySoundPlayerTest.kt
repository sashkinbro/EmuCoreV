package com.sbro.emucorev.core

import android.content.Context
import android.net.Uri
import com.sbro.emucorev.data.TrophySoundMode
import com.sbro.emucorev.data.TrophySoundSettings
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class TrophySoundPlayerTest {
    private val context: Context = RuntimeEnvironment.getApplication()

    @Test
    fun customPlaybackErrorReleasesAndFallsBackOnlyOnce() {
        val factory = FakeFactory()
        val failedCustom = LinkedBlockingQueue<Uri>()
        val system = Uri.parse("content://system/notification")
        val player = TrophySoundPlayer(context, failedCustom::offer, factory, { system })

        player.play(TrophySoundSettings(TrophySoundMode.CUSTOM, Uri.parse("content://custom/trophy")))
        val customBackend = factory.nextBackend()
        assertTrue(customBackend.awaitReady())
        customBackend.error()

        assertTrue(customBackend.awaitClosed())
        assertEquals(Uri.parse("content://custom/trophy"), failedCustom.poll(2, TimeUnit.SECONDS))
        val fallback = factory.nextBackend()
        assertTrue(fallback.awaitReady())
        assertEquals(system, fallback.uri)

        customBackend.error()
        assertNullWithin(failedCustom)
        player.close()
        assertTrue(fallback.awaitClosed())
    }

    @Test
    fun staleCallbacksCannotReplaceNewSoundOrMarkItUnavailable() {
        val factory = FakeFactory()
        val failedCustom = LinkedBlockingQueue<Uri>()
        val player = TrophySoundPlayer(context, failedCustom::offer, factory, { Uri.parse("content://system/tone") })
        val oldUri = Uri.parse("content://custom/old")
        val newUri = Uri.parse("content://custom/new")

        player.play(TrophySoundSettings(TrophySoundMode.CUSTOM, oldUri))
        val stale = factory.nextBackend()
        assertTrue(stale.awaitReady())
        player.play(TrophySoundSettings(TrophySoundMode.CUSTOM, newUri))
        val current = factory.nextBackend()
        assertTrue(current.awaitReady())
        stale.error()
        stale.prepared()

        assertTrue(stale.awaitClosed())
        assertNullWithin(failedCustom)
        assertFalse(current.closed)
        current.prepared()
        assertTrue(current.awaitStarted())
        player.close()
        assertTrue(current.awaitClosed())
    }

    @Test
    fun completionAndCloseReleaseBackendAndPrepareFailureUsesFallback() {
        val factory = FakeFactory()
        val player = TrophySoundPlayer(context, {}, factory, { Uri.parse("content://system/tone") })
        player.play(TrophySoundSettings(TrophySoundMode.SYSTEM))
        val first = factory.nextBackend()
        assertTrue(first.awaitReady())
        first.prepared()
        assertTrue("system playback did not start; closed=${first.closed} uri=${first.uri}", first.awaitStarted())
        first.complete()
        assertTrue(first.awaitClosed())

        factory.failNextPrepare = true
        player.play(TrophySoundSettings(TrophySoundMode.CUSTOM, Uri.parse("content://custom/broken")))
        val failed = factory.nextBackend()
        assertTrue(failed.awaitReady())
        assertTrue(failed.awaitClosed())
        val fallback = factory.nextBackend()
        assertTrue(fallback.awaitReady())
        assertEquals(Uri.parse("content://system/tone"), fallback.uri)
        player.close()
        assertTrue(fallback.awaitClosed())
    }

    private fun assertNullWithin(queue: LinkedBlockingQueue<Uri>) {
        assertEquals(null, queue.poll(150, TimeUnit.MILLISECONDS))
    }

    private class FakeFactory : TrophySoundBackendFactory {
        private val backends = LinkedBlockingQueue<FakeBackend>()
        private val created = AtomicInteger()
        @Volatile var failNextPrepare = false

        override fun create(context: Context): TrophySoundBackend = FakeBackend().also { backend ->
            backend.failPrepare = failNextPrepare
            failNextPrepare = false
            created.incrementAndGet()
            backends.offer(backend)
        }

        fun nextBackend(): FakeBackend = checkNotNull(backends.poll(2, TimeUnit.SECONDS)) {
            "Expected backend #${created.get()} to be created"
        }
    }

    private class FakeBackend : TrophySoundBackend {
        @Volatile var uri: Uri? = null
        @Volatile var closed = false
        @Volatile var startCount = 0
        @Volatile var failPrepare = false
        private val closedLatch = java.util.concurrent.CountDownLatch(1)
        private val startedLatch = java.util.concurrent.CountDownLatch(1)
        private val readyLatch = java.util.concurrent.CountDownLatch(1)
        private var onPrepared: (() -> Unit)? = null
        private var onCompletion: (() -> Unit)? = null
        private var onError: (() -> Unit)? = null

        override fun prepare(uri: Uri, onPrepared: () -> Unit, onCompletion: () -> Unit, onError: () -> Unit) {
            this.uri = uri
            this.onPrepared = onPrepared
            this.onCompletion = onCompletion
            this.onError = onError
            readyLatch.countDown()
            if (failPrepare) error("synthetic prepare failure")
        }

        override fun start() {
            startCount++
            startedLatch.countDown()
        }

        override fun close() {
            closed = true
            closedLatch.countDown()
        }

        fun prepared() = onPrepared?.invoke()
        fun complete() = onCompletion?.invoke()
        fun error() = onError?.invoke()
        fun awaitReady() = readyLatch.await(2, TimeUnit.SECONDS)
        fun awaitClosed() = closedLatch.await(2, TimeUnit.SECONDS)
        fun awaitStarted() = startedLatch.await(2, TimeUnit.SECONDS)
    }
}
