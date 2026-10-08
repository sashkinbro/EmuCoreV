package com.sbro.emucorev.ui.cheats

import com.sbro.emucorev.core.VitaCheatEntry
import com.sbro.emucorev.core.VitaCheatSnapshot
import org.junit.Assert.*
import org.junit.Test

class CheatPackIntegrityTest {
    @Test
    fun verifiesCanonicalLfDigestAcrossLineEndings() {
        // SHA-256 of the bytes "abc\n".
        val digest = "edeaaff3f1774ad2888673770c6d64097e391bc362d7d6fb34982ddf0efd18cb"
        assertTrue(verifyCheatPack("abc\n".toByteArray(), digest))
        assertTrue(verifyCheatPack("abc\r\n".toByteArray(), digest))
        assertTrue(verifyCheatPack("abc\r".toByteArray(), digest))
        assertFalse(verifyCheatPack("abd\n".toByteArray(), digest))
        assertFalse(verifyCheatPack("abc\n".toByteArray(), "invalid"))
    }

    @Test
    fun installedIdentitySurvivesTogglesButDetectsAnotherPack() {
        val cheat = VitaCheatEntry("Health", false, false, false, "$0200 81000000 00000001\n")
        val snapshot = VitaCheatSnapshot("PCSE00001", "header", "/pack.psv", false, listOf(cheat))
        assertEquals(cheatPackFingerprint(snapshot), cheatPackFingerprint(snapshot.copy(
            masterEnabled = true, cheats = listOf(cheat.copy(enabled = true, enabledOnBoot = true))
        )))
        assertNotEquals(cheatPackFingerprint(snapshot), cheatPackFingerprint(snapshot.copy(
            cheats = listOf(cheat.copy(codes = "$0200 81000000 00000002\n"))
        )))
    }
}
