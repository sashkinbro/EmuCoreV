package com.sbro.emucorev.core

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class LaunchGateTest {
    private val dir = Files.createTempDirectory("launch-gate").toFile()
    private var now = 1_000_000L
    private fun gate() = LaunchGate(File(dir, "launch-pending"), { now }, 15_000L)

    @Test fun secondLaunchIsBusyUntilTheEmulatorReleasesTheGate() {
        val caller = gate()
        assertTrue(caller.tryAcquire())
        // A different process sees the same marker file.
        val otherProcess = gate()
        assertFalse(otherProcess.tryAcquire())
        caller.release()
        assertTrue(otherProcess.tryAcquire())
    }

    @Test fun pendingLaunchStopsBlockingAfterTheTimeout() {
        assertTrue(gate().tryAcquire())
        now += 14_999
        assertFalse(gate().tryAcquire())
        now += 1
        assertTrue(gate().tryAcquire())
    }

    @Test fun clockMovingBackwardsDoesNotWedgeTheGate() {
        assertTrue(gate().tryAcquire())
        now -= 60_000
        assertTrue(gate().tryAcquire())
    }

    @Test fun garbageMarkerIsTreatedAsReleased() {
        File(dir, "launch-pending").writeText("not a number")
        assertTrue(gate().tryAcquire())
    }

    @Test fun sessionMarkerReportsLiveOnlyForAliveProcess() {
        val marker = File(dir, "game-session")
        val alive = mutableSetOf<Int>()
        val session = GameSessionMarker(marker, { pid, _ -> pid in alive })
        assertFalse(session.isLive())
        session.markLive(4242, "com.sbro.emucorev")
        assertFalse(session.isLive())
        alive += 4242
        assertTrue(session.isLive())
        // Another pid cannot clear a session it does not own.
        session.clear(1)
        assertTrue(marker.isFile)
        session.clear(4242)
        assertFalse(marker.isFile)
        assertFalse(session.isLive())
    }
}
