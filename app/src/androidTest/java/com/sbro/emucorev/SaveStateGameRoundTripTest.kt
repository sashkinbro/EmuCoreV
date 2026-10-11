package com.sbro.emucorev

import android.os.SystemClock
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.core.PerformanceStatsBridge
import com.sbro.emucorev.core.SaveStateBridge
import com.sbro.emucorev.core.VitaLaunchBridge
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.io.File

/** Opt-in real-game probe. All output goes to cache, leaving library slots intact. */
class SaveStateGameRoundTripTest {
    @Test fun restoreSaveRestoreAndResave() = runBlocking {
        val title = InstrumentationRegistry.getArguments().getString("stateProbe").orEmpty()
        assumeTrue("An installed title must be explicitly selected", title.matches(Regex("PCS[A-Z][0-9]{5}")))
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val root = EmulatorStorage.runtimeRoot(context)
        val source = File(root, "savestates/$title/quick.ecvs")
        assertTrue(source.isFile)
        val output = File(context.externalCacheDir, "state-roundtrip-probe/$title").apply { mkdirs() }
        assertEquals(VitaLaunchBridge.LaunchResult.Success,
            VitaLaunchBridge.launchInstalledTitleWithSaveState(context, title, source.absolutePath))
        val deadline = SystemClock.elapsedRealtime() + 120_000
        var ready = false
        while (SystemClock.elapsedRealtime() < deadline) {
            if (SaveStateBridge.runningTitleId() == title && PerformanceStatsBridge.snapshot().fps != null) {
                ready = true
                break
            }
            Thread.sleep(250)
        }
        assertTrue("Restored game must render", ready)
        Thread.sleep(3000)
        val saved = File(output, "saved.ecvs")
        val save = SaveStateBridge.save(saved.absolutePath, BuildConfig.VERSION_NAME)
        assertTrue("Save: ${save.status}: ${save.error}", save.isOk)
        val load = SaveStateBridge.load(saved.absolutePath, true)
        assertTrue("Warm load: ${load.status}: ${load.error}", load.isOk)
        Thread.sleep(3000)
        val resave = SaveStateBridge.save(File(output, "resaved.ecvs").absolutePath, BuildConfig.VERSION_NAME)
        assertTrue("Save after restore: ${resave.status}: ${resave.error}", resave.isOk)
        assertEquals(title, resave.titleId)
    }
}
