package com.sbro.emucorev.core

import android.content.Intent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class LabConfigTest {
    private fun labIntent() = Intent("com.sbro.emucorev.action.LAUNCH")
        .putExtra(LaunchIntents.APP_RESTART_PARAMETERS, arrayOf("-r", "PCSE00000", "-loadstate", "/x/lab/saves/PCSE00000/a.ecvs"))

    @Test fun defaults() {
        val cfg = LabConfig.fromIntent(labIntent().putExtra("ecv.lab", true).putExtra("ecv.lab.run_id", "r1"))
        assertNotNull(cfg)
        assertEquals("r1", cfg!!.runId)
        assertNull(cfg.frameCapture)
        assertFalse(cfg.gpuTrace)
        assertTrue(cfg.statsOverlay)
        assertEquals(5, cfg.perfIntervalSeconds)
    }

    @Test fun allExtras() {
        val cfg = LabConfig.fromIntent(labIntent().putExtra("ecv.lab", true).putExtra("ecv.lab.run_id", "run_2.a-b")
            .putExtra("ecv.lab.frame_capture", 3).putExtra("ecv.lab.gpu_trace", true)
            .putExtra("ecv.lab.stats_overlay", false).putExtra("ecv.lab.perf_interval_s", 2))!!
        assertEquals(3, cfg.frameCapture)
        assertTrue(cfg.gpuTrace)
        assertFalse(cfg.statsOverlay)
        assertEquals(2, cfg.perfIntervalSeconds)
    }

    @Test fun rejectsMissingFlagOrBadRunId() {
        assertNull(LabConfig.fromIntent(labIntent().putExtra("ecv.lab.run_id", "r1")))
        assertNull(LabConfig.fromIntent(labIntent().putExtra("ecv.lab", true)))
        assertNull(LabConfig.fromIntent(labIntent().putExtra("ecv.lab", true).putExtra("ecv.lab.run_id", "a b")))
        assertNull(LabConfig.fromIntent(labIntent().putExtra("ecv.lab", true).putExtra("ecv.lab.run_id", "")))
        assertNull(LabConfig.fromIntent(null))
    }

    @Test fun clampsAndSanitisesNumbers() {
        val cfg = LabConfig.fromIntent(labIntent().putExtra("ecv.lab", true).putExtra("ecv.lab.run_id", "r")
            .putExtra("ecv.lab.frame_capture", 100000).putExtra("ecv.lab.perf_interval_s", 0))!!
        assertEquals(LabConfig.MAX_FRAME_CAPTURE, cfg.frameCapture)
        assertEquals(5, cfg.perfIntervalSeconds)
        assertNull(LabConfig.fromIntent(labIntent().putExtra("ecv.lab", true).putExtra("ecv.lab.run_id", "r")
            .putExtra("ecv.lab.frame_capture", -4))!!.frameCapture)
    }

    @Test fun labLaunchNeverAutoLoadsQuickSave() {
        val args = LaunchIntents.launchArguments("PCSE00000", "/p/a.ecvs")
        assertTrue(shouldSkipExitAutoSave(args, automaticLoad = false))
    }
}
