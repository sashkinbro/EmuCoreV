package com.sbro.emucorev.ui.savestates

import com.sbro.emucorev.core.SaveStateResult
import com.sbro.emucorev.core.VitaLaunchBridge
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SaveStateLoadRoutingTest {
    @Test
    fun rendererMismatchRelaunchesTheSelectedTitleAndSlot() = runBlocking {
        val titleId = "PCSE00120"
        val selectedSlot = 4
        var launchArguments: Pair<String, Int>? = null

        val outcome = loadSaveStateForRunningTitle(
            titleId = titleId,
            slot = selectedSlot,
            load = { result(SaveStateResult.STATUS_CONFIGURATION_MISMATCH) },
            launchWithState = { requestedTitle, requestedSlot ->
                launchArguments = requestedTitle to requestedSlot
                VitaLaunchBridge.LaunchResult.Success
            }
        )

        assertEquals(titleId to selectedSlot, launchArguments)
        assertEquals(
            SaveStateLoadOutcome.Relaunched(VitaLaunchBridge.LaunchResult.Success),
            outcome
        )
    }

    @Test
    fun ordinaryLoadResultDoesNotRelaunch() = runBlocking {
        var launched = false
        val loaded = result(SaveStateResult.STATUS_OK)

        val outcome = loadSaveStateForRunningTitle(
            titleId = "PCSE00120",
            slot = 2,
            load = { loaded },
            launchWithState = { _, _ ->
                launched = true
                VitaLaunchBridge.LaunchResult.Success
            }
        )

        assertFalse(launched)
        assertEquals(SaveStateLoadOutcome.Loaded(loaded), outcome)
    }

    @Test
    fun unusableRendererMismatchDoesNotRelaunch() = runBlocking {
        var launched = false
        val unusable = result(SaveStateResult.STATUS_CONFIGURATION_MISMATCH).copy(sessionUsable = false)

        val outcome = loadSaveStateForRunningTitle(
            titleId = "PCSE00120",
            slot = 3,
            load = { unusable },
            launchWithState = { _, _ ->
                launched = true
                VitaLaunchBridge.LaunchResult.Success
            }
        )

        assertFalse(launched)
        assertEquals(SaveStateLoadOutcome.Loaded(unusable), outcome)
    }

    @Test
    fun invalidLoadResultDoesNotRelaunch() = runBlocking {
        var launched = false
        val invalid = result(SaveStateResult.STATUS_INVALID_FILE).copy(error = "corrupt state")

        val outcome = loadSaveStateForRunningTitle(
            titleId = "PCSE00120",
            slot = 1,
            load = { invalid },
            launchWithState = { _, _ ->
                launched = true
                VitaLaunchBridge.LaunchResult.Success
            }
        )

        assertFalse(launched)
        assertEquals(SaveStateLoadOutcome.Loaded(invalid), outcome)
    }

    @Test
    fun rendererMismatchReturnsTheLaunchFailureForTheScreenToReport() = runBlocking {
        val outcome = loadSaveStateForRunningTitle(
            titleId = "PCSE00120",
            slot = 5,
            load = { result(SaveStateResult.STATUS_CONFIGURATION_MISMATCH) },
            launchWithState = { _, _ -> VitaLaunchBridge.LaunchResult.MissingFirmware }
        )

        assertTrue(outcome is SaveStateLoadOutcome.Relaunched)
        assertEquals(
            VitaLaunchBridge.LaunchResult.MissingFirmware,
            (outcome as SaveStateLoadOutcome.Relaunched).result
        )
    }

    @Test
    fun launchExceptionBecomesFailureOutcome() = runBlocking {
        val outcome = loadSaveStateForRunningTitle(
            titleId = "PCSE00120",
            slot = 5,
            load = { result(SaveStateResult.STATUS_CONFIGURATION_MISMATCH) },
            launchWithState = { _, _ -> error("launch failed") }
        )

        assertEquals(
            SaveStateLoadOutcome.Relaunched(VitaLaunchBridge.LaunchResult.Failure),
            outcome
        )
    }

    @Test
    fun launchCancellationPropagates() = runBlocking {
        var propagated = false
        try {
            loadSaveStateForRunningTitle(
                titleId = "PCSE00120",
                slot = 5,
                load = { result(SaveStateResult.STATUS_CONFIGURATION_MISMATCH) },
                launchWithState = { _, _ -> throw CancellationException("cancelled") }
            )
        } catch (_: CancellationException) {
            propagated = true
        }

        assertTrue(propagated)
    }

    private fun result(status: String) = SaveStateResult(
        status = status,
        error = "",
        bytes = 0L,
        titleId = "PCSE00120",
        appVersion = "test",
        engineVersion = 11,
        sessionMatch = true,
        timestamp = 0L,
        sessionUsable = true
    )
}
