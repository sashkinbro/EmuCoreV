package com.sbro.emucorev.ui.emulation

import org.junit.Assert.*
import org.junit.Test

class MenuSnapshotLifecycleTest {
    @Test fun onlyMaliVulkanUsesTheWorkaround() {
        assertTrue(usesPausedMenuBackdrop(true, "Vulkan"))
        assertFalse(usesPausedMenuBackdrop(false, "Vulkan"))
        assertFalse(usesPausedMenuBackdrop(true, "OpenGL"))
        assertFalse(usesPausedMenuBackdrop(false, ""))
    }

    @Test fun captureKeepsAspectRatioAndNeverExceedsTheMemoryCap() {
        assertEquals(1920 to 1080, menuSnapshotSize(3840, 2160))
        assertEquals(1080 to 1920, menuSnapshotSize(2160, 3840))
        assertEquals(960 to 544, menuSnapshotSize(960, 544))
        assertNull(menuSnapshotSize(0, 544))
        assertNull(menuSnapshotSize(960, -1))
    }

    @Test fun completionIsAcceptedOnlyOnceBeforeDeadline() {
        val gate = MenuSnapshotGate(200)
        val token = gate.generation
        assertTrue(gate.complete(token, 199))
        assertFalse(gate.complete(token, 199))
    }

    @Test fun lateCallbackIsRejectedEvenIfTheTimeoutRunnableWasDelayed() {
        val gate = MenuSnapshotGate(200)
        assertFalse(gate.complete(gate.generation, 200))
    }

    @Test fun timeoutInvalidatesAnInFlightCallback() {
        val gate = MenuSnapshotGate(200)
        val token = gate.generation
        gate.expire()
        assertNotEquals(token, gate.generation)
        assertFalse(gate.complete(token, 100))
    }

    @Test fun closedMenuCannotPublishIntoTheNextMenuGeneration() {
        val closed = MenuSnapshotGate(200)
        val token = closed.generation
        closed.dispose()
        val reopened = MenuSnapshotGate(300)
        assertFalse(closed.complete(token, 100))
        assertTrue(reopened.complete(reopened.generation, 100))
    }
}
