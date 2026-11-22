package com.sbro.emucorev.core

import android.app.Application
import android.content.Intent
import java.io.File
import kotlin.concurrent.thread
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import org.robolectric.shadows.ShadowLooper

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], application = Application::class)
class LaunchIntentsTest {
    private val context get() = RuntimeEnvironment.getApplication()

    @Test fun freshLaunchClearsTheRetainedTask() {
        val intent = LaunchIntents.buildGameLaunch(context, "PCSE00120", null, false, gameSessionLive = false, nowMs = 7)
        assertTrue(intent.flags and Intent.FLAG_ACTIVITY_NEW_TASK != 0)
        assertTrue(intent.flags and Intent.FLAG_ACTIVITY_CLEAR_TASK != 0)
        assertEquals("LAUNCH_PCSE00120_7", intent.action)
        assertArrayEquals(arrayOf("-r", "PCSE00120"), intent.getStringArrayExtra(LaunchIntents.APP_RESTART_PARAMETERS))
        assertFalse(intent.hasExtra(AUTOMATIC_STATE_LOAD_EXTRA))
    }

    @Test fun launchWhileAGameRunsReachesOnNewIntentInsteadOfClearingTheTask() {
        val intent = LaunchIntents.buildGameLaunch(context, "PCSE00120", "/s/quick.ecvs", true, gameSessionLive = true)
        assertTrue(intent.flags and Intent.FLAG_ACTIVITY_NEW_TASK != 0)
        assertEquals(0, intent.flags and Intent.FLAG_ACTIVITY_CLEAR_TASK)
        assertArrayEquals(arrayOf("-r", "PCSE00120", "-loadstate", "/s/quick.ecvs"),
            intent.getStringArrayExtra(LaunchIntents.APP_RESTART_PARAMETERS))
        assertTrue(intent.getBooleanExtra(com.sbro.emucorev.core.vita.NativeLibraryMode.RETURN_TO_NATIVE_LIBRARY, false))
    }

    @Test fun restoredLaunchLosesItsExplicitStateButKeepsTheTitle() {
        val intent = LaunchIntents.buildGameLaunch(context, "PCSE00120", "/s/slot_1.ecvs", false, false)
        assertTrue(LaunchIntents.sanitizeRestoredLaunch(intent, restoredByFramework = true))
        assertArrayEquals(arrayOf("-r", "PCSE00120"), intent.getStringArrayExtra(LaunchIntents.APP_RESTART_PARAMETERS))
        assertTrue(intent.getBooleanExtra(SKIP_AUTOMATIC_STATE_LOAD_EXTRA, false))
    }

    @Test fun launchedFromHistoryIsTreatedAsRestored() {
        val intent = LaunchIntents.buildGameLaunch(context, "PCSE00120", "/s/slot_1.ecvs", false, false)
            .addFlags(Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY)
        assertTrue(LaunchIntents.sanitizeRestoredLaunch(intent, restoredByFramework = false))
        assertFalse(intent.getStringArrayExtra(LaunchIntents.APP_RESTART_PARAMETERS)!!.contains("-loadstate"))
    }

    @Test fun freshLaunchKeepsItsExplicitState() {
        val intent = LaunchIntents.buildGameLaunch(context, "PCSE00120", "/s/slot_1.ecvs", false, false)
        assertFalse(LaunchIntents.sanitizeRestoredLaunch(intent, restoredByFramework = false))
        assertTrue(intent.getStringArrayExtra(LaunchIntents.APP_RESTART_PARAMETERS)!!.contains("-loadstate"))
    }

    // Bridge: every caller only sends the Intent, and a pending launch yields Busy.

    private fun installFirmware() {
        val root = EmulatorStorage.vitaRoot(context)
        File(root, "vs0").mkdirs(); File(root, "vs0/file").writeText("x")
        File(root, "sa0").mkdirs(); File(root, "sa0/file").writeText("x")
    }

    private fun launch(titleId: String, statePath: String? = null): VitaLaunchBridge.LaunchResult {
        var result: VitaLaunchBridge.LaunchResult? = null
        val worker = thread {
            result = runBlocking {
                if (statePath == null) VitaLaunchBridge.launchInstalledTitle(context, titleId)
                else VitaLaunchBridge.launchInstalledTitleWithSaveState(context, titleId, statePath)
            }
        }
        // The bridge hops to the main looper to start the Activity.
        while (worker.isAlive) {
            ShadowLooper.idleMainLooper()
            worker.join(10)
        }
        return result!!
    }

    @Test fun bridgeSendsOnlyTheIntentAndDebouncesUntilReady() {
        installFirmware()
        LaunchGate.forContext(context).release()
        assertEquals(VitaLaunchBridge.LaunchResult.Success, launch("PCSE00120", "/s/quick.ecvs"))
        val started = shadowOf(context).nextStartedActivity
        assertNotNull(started)
        assertEquals("com.sbro.emucorev.core.vita.Emulator", started.component?.className)
        assertArrayEquals(arrayOf("-r", "PCSE00120", "-loadstate", "/s/quick.ecvs"),
            started.getStringArrayExtra(LaunchIntents.APP_RESTART_PARAMETERS))
        // The Emulator has not reported ready yet: a second launch must not start anything.
        assertEquals(VitaLaunchBridge.LaunchResult.Busy, launch("PCSE00121"))
        assertNull(shadowOf(context).nextStartedActivity)
        // The Emulator process releases the gate once its preparation is done.
        LaunchGate.forContext(context).release()
        assertEquals(VitaLaunchBridge.LaunchResult.Success, launch("PCSE00121"))
        LaunchGate.forContext(context).release()
    }

    @Test fun missingFirmwareIsReportedWithoutHoldingTheGate() {
        val root = EmulatorStorage.vitaRoot(context)
        File(root, "vs0").deleteRecursively()
        LaunchGate.forContext(context).release()
        assertEquals(VitaLaunchBridge.LaunchResult.MissingFirmware, launch("PCSE00120"))
        assertFalse(LaunchGate.forContext(context).isPending())
        assertNull(shadowOf(context).nextStartedActivity)
    }
}
