package com.sbro.emucorev.core

import android.app.ActivityManager
import android.content.pm.ActivityInfo
import android.os.SystemClock
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry
import androidx.test.runner.lifecycle.Stage
import com.sbro.emucorev.MainActivity
import com.sbro.emucorev.core.vita.Emulator
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Assume.assumeNotNull
import org.junit.Test
import org.junit.runner.RunWith

/** Opt in with -e titleId <installed title>; does not install or delete user data. */
@RunWith(AndroidJUnit4::class)
class GameExitInstrumentedTest {
    @Test fun twoGameSessionsReturnToTheSameLibraryActivity() {
        val titleId = InstrumentationRegistry.getArguments().getString("titleId")
        assumeNotNull(titleId)
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            lateinit var original: MainActivity
            scenario.onActivity { original = it }
            if (InstrumentationRegistry.getArguments().getString("portraitLibrary") == "true") {
                scenario.onActivity { it.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT }
            }
            instrumentation.waitForIdleSync()
            SystemClock.sleep(1_500)
            repeat(2) {
                assertEquals(VitaLaunchBridge.LaunchResult.Success,
                    runBlocking { VitaLaunchBridge.launchInstalledTitle(original, titleId!!) })
                // Lenovo OneVision can leave a singleInstance task hidden when
                // instrumentation has just brought the scenario task forward.
                SystemClock.sleep(500)
                original.getSystemService(ActivityManager::class.java).appTasks
                    .firstOrNull { it.taskInfo?.baseActivity?.className == Emulator::class.java.name }
                    ?.moveToFront()
                await("Game did not start") {
                    SaveStateBridge.runningTitleId() == titleId && resumedEmulator() != null
                }
                // Exercise actual rendering and shutdown, rather than cancelling startup.
                SystemClock.sleep(5_000)
                instrumentation.runOnMainSync {
                    checkNotNull(resumedEmulator()).exitEmulation(autoSave = false)
                }
                await("The original library did not resume") {
                    var same = false
                    instrumentation.runOnMainSync {
                        same = ActivityLifecycleMonitorRegistry.getInstance()
                            .getActivitiesInStage(Stage.RESUMED).any { it === original }
                    }
                    same
                }
                assertFalse("Library Activity was destroyed", original.isDestroyed)
                assertEquals("Native title remained active after exit", "", SaveStateBridge.runningTitleId())
            }
        }
    }

    private fun resumedEmulator(): Emulator? {
        var activity: Emulator? = null
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        if (android.os.Looper.myLooper() == android.os.Looper.getMainLooper()) {
            return ActivityLifecycleMonitorRegistry.getInstance()
                .getActivitiesInStage(Stage.RESUMED).filterIsInstance<Emulator>().firstOrNull()
        }
        instrumentation.runOnMainSync { activity = resumedEmulator() }
        return activity
    }

    private fun await(message: String, condition: () -> Boolean) {
        val deadline = SystemClock.elapsedRealtime() + 60_000
        while (!condition()) {
            if (SystemClock.elapsedRealtime() >= deadline) fail(message)
            SystemClock.sleep(100)
        }
    }
}
