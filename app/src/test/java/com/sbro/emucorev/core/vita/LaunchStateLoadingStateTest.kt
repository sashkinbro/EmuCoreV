package com.sbro.emucorev.core.vita

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class LaunchStateLoadingStateTest {
    @Test
    fun onlyLaunchArgumentsWithAStatePathSeedTheLoadingOverlay() {
        assertTrue(LaunchStateLoadingState.fromArguments(arrayOf("-r", "PCSE00001", "-loadstate", "/state/slot" )).active)
        assertFalse(LaunchStateLoadingState.fromArguments(arrayOf("-r", "PCSE00001")).active)
        assertFalse(LaunchStateLoadingState.fromArguments(arrayOf("-r", "PCSE00001", "-loadstate", " ")).active)
        assertFalse(LaunchStateLoadingState.fromArguments(arrayOf("-r", "PCSE00001", "-loadstate")).active)
    }

    @Test
    fun progressIsClampedAndNeverMovesBackward() {
        val seeded = LaunchStateLoadingState.fromArguments(arrayOf("-r", "PCSE00001", "-loadstate", "/state"))
        val halfway = seeded.onNativeProgress(0.6f, active = true)

        assertEquals(0.6f, halfway.progress)
        assertEquals(0.6f, halfway.onNativeProgress(0.2f, active = true).progress)
        assertEquals(1f, halfway.onNativeProgress(4f, active = true).progress)
        assertEquals(0f, seeded.onNativeProgress(Float.NaN, active = true).progress)
    }

    @Test
    fun completionAndFailureDismissAndRejectStaleCallbacks() {
        val seeded = LaunchStateLoadingState.fromArguments(arrayOf("-r", "PCSE00001", "-loadstate", "/state"))
        val completed = seeded.onNativeProgress(0.93f, active = false)
        assertFalse(completed.active)
        assertEquals(1f, completed.progress)
        assertEquals(completed, completed.onNativeProgress(0.5f, active = true))

        val failed = seeded.onFailure()
        assertFalse(failed.active)
        assertEquals(failed, failed.onNativeProgress(0.8f, active = true))
    }
}
