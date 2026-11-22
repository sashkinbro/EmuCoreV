package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class EmulatorStorageDecisionTest {
    private val internal = "/storage/emulated/0/Android/data/app/files"
    private val sd = "/storage/ABCD-1234/Android/data/app/files"

    @Test fun noSelectionUsesFirstRootAndIsAvailable() {
        val decision = EmulatorStorage.decideStorageRoot(null, listOf(internal, sd))
        assertEquals(internal, decision.rootPath)
        assertFalse(decision.selectedUnavailable)
    }

    @Test fun presentSelectionIsUsed() {
        val decision = EmulatorStorage.decideStorageRoot(sd, listOf(internal, sd))
        assertEquals(sd, decision.rootPath)
        assertFalse(decision.selectedUnavailable)
    }

    @Test fun missingSelectionIsUnavailableNotSilentlyReplaced() {
        val decision = EmulatorStorage.decideStorageRoot(sd, listOf(internal))
        assertTrue(decision.selectedUnavailable)
        assertEquals(internal, decision.rootPath)
    }

    @Test fun unmountedSelectedMediaIsUnavailable() {
        assertTrue(EmulatorStorage.decideStorageRoot(sd, listOf(internal, sd), selectedMediaUsable = false).selectedUnavailable)
        assertTrue(EmulatorStorage.isMediaStateUsable("mounted"))
        assertTrue(EmulatorStorage.isMediaStateUsable("unknown"))
        assertFalse(EmulatorStorage.isMediaStateUsable("removed"))
        assertFalse(EmulatorStorage.isMediaStateUsable("unmounted"))
        assertFalse(EmulatorStorage.isMediaStateUsable("mounted_ro"))
    }

    @Test fun installMarkerGuardsStagingUntilStale() {
        val now = 10_000_000L
        assertTrue(EmulatorStorage.isInstallMarkerActive(now - 1_000, now))
        assertFalse(EmulatorStorage.isInstallMarkerActive(now - EmulatorStorage.INSTALL_MARKER_STALE_MS - 1, now))
        assertFalse(EmulatorStorage.isInstallMarkerActive(0L, now))
    }
}
