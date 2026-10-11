package com.sbro.emucorev

import android.os.SystemClock
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorev.core.*
import com.sbro.emucorev.ui.cheats.CheatVersionCompatibility
import com.sbro.emucorev.ui.cheats.cheatVersionCompatibility
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.io.File

/** Explicit device probe; never touches library slots or enables a cheat on another revision. */
class CheatGameRoundTripTest {
    @Test fun spiderManCheatAndStateRoundTrip() = runBlocking {
        val args = InstrumentationRegistry.getArguments()
        assumeTrue("Requires an explicit supported-game device probe", args.getString("cheatProbe") == "PCSE00333")
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val title = "PCSE00333"
        assertEquals(CheatVersionCompatibility.MATCH,
            cheatVersionCompatibility(VitaSfoParser.parse(EmulatorStorage.paramSfoPath(context, title)).version, "1.00"))
        val runtime = EmulatorStorage.runtimeRoot(context)
        val source = File(runtime, "savestates/$title/slot_1.ecvs")
        assertTrue("Existing test state is required", source.isFile)
        val pack = File(runtime, "cheats/$title.psv")
        val oldPack = if (pack.exists()) pack.readBytes() else null
        val output = File(context.externalCacheDir, "cheat-roundtrip-probe").apply { mkdirs() }
        try {
            pack.parentFile!!.mkdirs()
            pack.writeText("#PCSE00333 US v1.00\n_V0 inf.HP\n\$B200 00000001 00000000\n\$0200 002A476C 00020000\n")
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
            assertTrue("Game must resume rendering after restore", ready)
            Thread.sleep(3000)
            val baseline = SaveStateBridge.save(File(output, "baseline.ecvs").absolutePath, BuildConfig.VERSION_NAME)
            assertTrue("Baseline save: ${baseline.status}: ${baseline.error}", baseline.isOk)
            val snapshot = CheatBridge.reload(title)
            assertEquals(1, snapshot.cheats.size)
            assertFalse(snapshot.cheats.single().broken)
            assertTrue(CheatBridge.setCheatEnabled(title, 0, true))
            assertTrue(CheatBridge.setCheatsEnabled(true))
            assertTrue(CheatBridge.snapshot(title).cheats.single().enabled)
            Thread.sleep(3000)
            val enabled = File(output, "enabled.ecvs")
            val saved = SaveStateBridge.save(enabled.absolutePath, BuildConfig.VERSION_NAME)
            assertTrue("Enabled save: ${saved.status}: ${saved.error}", saved.isOk)
            assertTrue(CheatBridge.setCheatEnabled(title, 0, false))
            val loaded = SaveStateBridge.load(enabled.absolutePath, true)
            assertTrue("Warm restore: ${loaded.status}: ${loaded.error}", loaded.isOk)
            Thread.sleep(3000)
            val resaved = SaveStateBridge.save(File(output, "resaved.ecvs").absolutePath, BuildConfig.VERSION_NAME)
            assertTrue("Save after restore: ${resaved.status}: ${resaved.error}", resaved.isOk)
        } finally {
            runCatching { CheatBridge.setCheatEnabled(title, 0, false) }
            if (oldPack == null) pack.delete() else pack.writeBytes(oldPack)
            runCatching { CheatBridge.reload(title) }
        }
    }
}
