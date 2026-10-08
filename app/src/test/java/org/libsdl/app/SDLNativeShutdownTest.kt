package org.libsdl.app

import android.app.Application
import android.os.Handler
import android.os.Looper
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import org.robolectric.annotation.LooperMode
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import kotlin.concurrent.thread
import kotlin.system.measureTimeMillis

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], application = Application::class)
@LooperMode(LooperMode.Mode.PAUSED)
class SDLNativeShutdownTest {
    class ShutdownActivity : SDLActivity() {
        val requests = AtomicInteger()
        val quits = AtomicInteger()
        var timeout = 2_000L
        fun shutdown(completion: () -> Unit) = performNativeShutdown(completion)
        fun destroy() = onDestroy()
        override fun sendQuitForShutdown() { requests.incrementAndGet() }
        override fun quitAfterShutdown() { quits.incrementAndGet() }
        override fun nativeShutdownTimeoutMs() = timeout
    }

    private lateinit var activity: ShutdownActivity
    private val release = CountDownLatch(1)
    private lateinit var guest: Thread

    @Before fun setUp() {
        activity = Robolectric.buildActivity(ShutdownActivity::class.java).get()
        SDLActivity.mSingleton = activity
        SDLActivity.mHIDDeviceManager = null
        SDLActivity.mBrokenLibraries = false
        SDLActivity.mHasNativeShutdown = false
        SDLActivity::class.java.getDeclaredField("nativeShutdownInFlight").apply { isAccessible = true }
            .setBoolean(null, false)
        guest = thread(name = "controlled-SDLThread") { release.await() }
        SDLActivity.mSDLThread = guest
    }

    @After fun tearDown() {
        release.countDown()
        guest.join(2_000)
        await { activity.requests.get() > 0 }
        repeat(5) { shadowOf(Looper.getMainLooper()).idle(); Thread.sleep(10) }
        SDLActivity.mSingleton = null
        SDLActivity.mSDLThread = null
        SDLActivity.mHasNativeShutdown = false
        SDLActivity::class.java.getDeclaredField("nativeShutdownInFlight").apply { isAccessible = true }
            .setBoolean(null, false)
    }

    @Test fun shutdownDoesNotBlockUiAndDestroySharesTheSameOperation() {
        var completions = 0
        val elapsed = measureTimeMillis {
            activity.shutdown { assertSame(Looper.getMainLooper(), Looper.myLooper()); completions++ }
            activity.destroy()
            activity.shutdown { completions++ }
        }
        assertTrue("UI blocked for $elapsed ms", elapsed < 500)
        var uiRan = false
        Handler(Looper.getMainLooper()).post { uiRan = true }
        shadowOf(Looper.getMainLooper()).idle()
        assertTrue(uiRan)
        assertEquals(0, completions)
        await { activity.requests.get() == 1 }
        assertEquals(0, activity.quits.get())
        release.countDown()
        await { completions == 2 }
        assertEquals(1, activity.requests.get())
        assertEquals(1, activity.quits.get())
        activity.shutdown { completions++ }
        assertEquals(3, completions)
    }

    @Test fun timeoutCompletesWithoutDestroyingLiveNativeState() {
        activity.timeout = 30L
        var completed = false
        activity.shutdown { completed = true }
        await { completed }
        assertTrue(guest.isAlive)
        assertEquals(1, activity.requests.get())
        assertEquals(0, activity.quits.get())
        val rejected = Robolectric.buildActivity(ShutdownActivity::class.java).create().get()
        assertTrue("A timed-out live session must still block SDL reinitialization", rejected.isFinishing)
        assertSame(activity, SDLActivity.mSingleton)
    }

    @Test fun nativeCallerIsMarshalledToMainBeforeClaimingShutdown() {
        var completed = false
        val caller = thread { activity.shutdown { completed = true } }
        caller.join(500)
        assertFalse(caller.isAlive)
        assertFalse(SDLActivity.mHasNativeShutdown)
        shadowOf(Looper.getMainLooper()).idle()
        assertTrue(SDLActivity.mHasNativeShutdown)
        release.countDown()
        await { completed }
        assertEquals(1, activity.quits.get())
    }

    @Test fun anotherActivityCannotInitializeSdlDuringShutdown() {
        var completed = false
        activity.shutdown { completed = true }
        val rejected = Robolectric.buildActivity(ShutdownActivity::class.java).create().get()
        assertTrue(rejected.isFinishing)
        assertSame(activity, SDLActivity.mSingleton)
        assertSame(guest, SDLActivity.mSDLThread)
        release.countDown()
        await { completed }
        assertEquals(1, activity.quits.get())
    }

    private fun await(condition: () -> Boolean) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3)
        while (!condition() && System.nanoTime() < deadline) {
            shadowOf(Looper.getMainLooper()).idle()
            Thread.sleep(5)
        }
        assertTrue("Shutdown did not reach the expected state", condition())
    }
}
