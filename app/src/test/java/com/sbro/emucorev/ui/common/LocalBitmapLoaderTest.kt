package com.sbro.emucorev.ui.common

import android.graphics.Bitmap
import android.graphics.Color
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class LocalBitmapLoaderTest {
    @Test fun decodingLeavesTheCallingThreadAndReusesCachedBitmap() = runBlocking {
        val caller = Thread.currentThread()
        var decodes = 0
        val expected = Bitmap.createBitmap(32, 32, Bitmap.Config.ARGB_8888)
        val loader = LocalBitmapLoader { _ ->
            assertNotSame(caller, Thread.currentThread())
            decodes++
            expected
        }
        assertSame(expected, loader.load("cover.png"))
        assertSame(expected, loader.load("cover.png"))
        assertEquals(1, decodes)
    }

    @Test fun targetDimensionsBoundBothPortraitAndLandscapeDecodes() {
        assertEquals(8, localBitmapSampleSize(4096, 2048))
        assertEquals(8, localBitmapSampleSize(2048, 4096))
        assertEquals(4, localBitmapSampleSize(1025, 513))
        assertEquals(1, localBitmapSampleSize(96, 64))
    }

    @Test fun blankPathsDoNotDecodeAndFailuresKeepThePlaceholder() = runBlocking {
        var decodes = 0
        val loader = LocalBitmapLoader { _ -> decodes++; error("invalid image") }
        assertNull(loader.load(null))
        assertNull(loader.load(" "))
        assertEquals(0, decodes)
        assertNull(loader.load("broken.png"))
    }

    @Test fun decodingDoesNotHoldTheCacheLock() {
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val executor = Executors.newFixedThreadPool(2)
        val loader = LocalBitmapLoader {
            entered.countDown()
            assertTrue(release.await(5, TimeUnit.SECONDS))
            Bitmap.createBitmap(16, 16, Bitmap.Config.ARGB_8888)
        }
        try {
            val loading = executor.submit<Bitmap?> { runBlocking { loader.load("cover.png") } }
            assertTrue(entered.await(5, TimeUnit.SECONDS))
            assertNull(executor.submit<Bitmap?> { loader.cached("other.png") }.get(1, TimeUnit.SECONDS))
            release.countDown()
            assertNotNull(loading.get(5, TimeUnit.SECONDS))
        } finally {
            release.countDown()
            executor.shutdownNow()
        }
    }

    @Test
    @GraphicsMode(GraphicsMode.Mode.NATIVE)
    fun realImageDecodeIsSampledAndKeepsTransparency() = runBlocking {
        val file = File.createTempFile("local-cover", ".png")
        try {
            val source = Bitmap.createBitmap(2048, 1024, Bitmap.Config.ARGB_8888)
            file.outputStream().use { source.compress(Bitmap.CompressFormat.PNG, 100, it) }
            source.recycle()
            val decoded = requireNotNull(LocalBitmapLoader().load(file.absolutePath))
            assertTrue(decoded.width <= 512)
            assertTrue(decoded.height <= 512)
            assertTrue(decoded.hasAlpha())
        } finally {
            file.delete()
        }
    }

    @Test
    @GraphicsMode(GraphicsMode.Mode.NATIVE)
    fun refreshLoadsNewPixelsWhenTheInstalledIconIsOverwrittenAtTheSamePath() = runBlocking {
        val file = File.createTempFile("installed-icon", ".png")
        fun write(color: Int) {
            val bitmap = Bitmap.createBitmap(16, 16, Bitmap.Config.ARGB_8888)
            bitmap.eraseColor(color)
            file.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
            bitmap.recycle()
        }
        try {
            val loader = LocalBitmapLoader()
            write(Color.RED)
            assertEquals(Color.RED, requireNotNull(loader.load(file.absolutePath)).getPixel(0, 0))
            write(Color.GREEN)
            loader.invalidate()
            assertEquals(Color.GREEN, requireNotNull(loader.load(file.absolutePath)).getPixel(0, 0))
        } finally {
            file.delete()
        }
    }
}
