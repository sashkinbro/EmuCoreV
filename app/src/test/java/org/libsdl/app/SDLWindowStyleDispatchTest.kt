@file:Suppress("DEPRECATION", "PLATFORM_CLASS_MAPPED_TO_KOTLIN")

package org.libsdl.app

import android.app.Application
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
import java.util.concurrent.atomic.AtomicReference
import kotlin.concurrent.thread
import kotlin.system.measureNanoTime

/** Exercises the actual SDL Handler/Looper path, without loading the native core or a device. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], application = Application::class)
@LooperMode(LooperMode.Mode.PAUSED)
class SDLWindowStyleDispatchTest {
    class CommandActivity : SDLActivity() {
        fun fullscreen(): Boolean = sendCommand(COMMAND_CHANGE_WINDOW_STYLE, 1)
        fun changeTitle(value: String): Boolean = sendCommand(COMMAND_CHANGE_TITLE, value)
    }

    private lateinit var activity: CommandActivity

    @Before fun setUp() {
        // Attach an Android Activity, but do not call SDL onCreate (which loads JNI).
        activity = Robolectric.buildActivity(CommandActivity::class.java).get()
        SDL.setContext(activity)
        SDLActivity.mSingleton = activity
        SDLActivity.mSurface = null // No first frame/resize callback yet, as during onCreate.
        SDLActivity.mFullscreenModeActive = false
    }

    @After fun tearDown() {
        shadowOf(Looper.getMainLooper()).idle()
        SDLActivity.mSingleton = null
        SDLActivity.mSurface = null
        SDLActivity.mFullscreenModeActive = false
        SDL.setContext(null)
    }

    @Test fun startupFullscreenRequestsNeverWaitForTheirOwnUiQueue() {
        assertSame(Looper.myLooper(), activity.commandHandler.looper)
        val elapsedMs = measureNanoTime {
            // SDL.onCreate + Emulator.onCreate/hideSystemBars + Emulator.onResume.
            repeat(3) { assertTrue(activity.fullscreen()) }
        } / 1_000_000
        println("Three queued startup fullscreen requests: $elapsedMs ms (host test, not device launch time)")

        // Old code spends 3 x 500 ms in Object.wait here. Allow scheduling noise,
        // but not even two of those waits. This is not a device launch benchmark.
        assertTrue("UI blocked for $elapsedMs ms dispatching three commands", elapsedMs < 750)
        assertFalse("Commands must retain their queued ordering", SDLActivity.mFullscreenModeActive)
        shadowOf(Looper.getMainLooper()).idle()
        assertTrue("Fullscreen must still be applied", SDLActivity.mFullscreenModeActive)
    }

    @Test fun uiThreadRenamedBySdlStillDoesNotWait() {
        val originalName = Thread.currentThread().name
        try {
            Thread.currentThread().name = "SDLActivity"
            val elapsedMs = measureNanoTime { repeat(3) { activity.fullscreen() } } / 1_000_000
            assertTrue("Use Looper identity, not the thread name ($elapsedMs ms)", elapsedMs < 750)
        } finally {
            Thread.currentThread().name = originalName
        }
    }

    @Test fun nativeThreadStillWaitsForResizeAndWakesOnSurfaceNotification() {
        val result = AtomicReference<Boolean>()
        val error = AtomicReference<Throwable>()
        val worker = thread(name = "SDLThread-test") {
            try {
                result.set(activity.fullscreen())
            } catch (failure: Throwable) {
                error.set(failure)
            }
        }
        try {
            val deadline = System.nanoTime() + 2_000_000_000L
            while (worker.isAlive && worker.state != Thread.State.TIMED_WAITING && System.nanoTime() < deadline) {
                Thread.sleep(1)
            }
            assertEquals("The native thread must retain the resize wait", Thread.State.TIMED_WAITING, worker.state)
            assertNull(result.get())
            shadowOf(Looper.getMainLooper()).idle()
            assertTrue(SDLActivity.mFullscreenModeActive)
            // Same monitor/notification used by SDLSurface.surfaceChanged.
            synchronized(activity) { (activity as java.lang.Object).notifyAll() }
            worker.join(250)
            assertFalse("Resize notification must release the native caller", worker.isAlive)
            assertNull(error.get())
            assertEquals(true, result.get())
        } finally {
            worker.interrupt()
            worker.join(2_000)
        }
    }

    @Test fun nativeResizeWaitRemainsBoundedWhenNoSurfaceCallbackArrives() {
        val elapsedMs = AtomicReference<Long>()
        val error = AtomicReference<Throwable>()
        val worker = thread(name = "SDLThread-timeout-test") {
            try {
                elapsedMs.set(measureNanoTime { activity.fullscreen() } / 1_000_000)
            } catch (failure: Throwable) {
                error.set(failure)
            }
        }
        try {
            worker.join(3_000)
            assertFalse("A missing resize must not hang native startup", worker.isAlive)
            assertNull(error.get())
            assertTrue("Preserve SDL's 500 ms bounded worker wait", (elapsedMs.get() ?: 0) >= 400)
        } finally {
            worker.interrupt()
            worker.join(2_000)
        }
    }

    @Test fun otherCommandsKeepTheirOriginalQueueOrder() {
        assertTrue(activity.changeTitle("first"))
        assertTrue(activity.fullscreen())
        assertTrue(activity.changeTitle("last"))
        shadowOf(Looper.getMainLooper()).idle()
        assertEquals("last", activity.title.toString())
        assertTrue(SDLActivity.mFullscreenModeActive)
    }
}
