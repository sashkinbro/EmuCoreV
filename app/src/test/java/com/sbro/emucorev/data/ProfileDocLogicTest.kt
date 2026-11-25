package com.sbro.emucorev.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ProfileDocLogicTest {
    private val ts = Any()

    private fun patches(existing: ExistingProfileDoc, name: String = "emailprefix", force: Boolean = false, photo: String? = null) =
        ProfileDocLogic.ensureProfilePatches(
            uid = "u1", email = "emailprefix@example.com", incomingName = name, photoURL = photo,
            forceName = force, existing = existing, playerTag = "EV-ABC", serverTimestamp = ts
        )

    // B060 / B065

    @Test fun signInDoesNotOverwriteAStoredDisplayName() {
        val result = patches(ExistingProfileDoc(exists = true, displayName = "Chosen Name"))
        assertEquals("Chosen Name", result.user["displayName"])
        assertEquals("chosen name", result.publicProfile["searchName"])
        assertEquals("Chosen Name", result.leaderboard["displayName"])
    }

    @Test fun explicitSignUpNameWinsOverTheFallbackStoredByTheListener() {
        val result = patches(ExistingProfileDoc(exists = true, displayName = "emailprefix"), name = "Real Name", force = true)
        assertEquals("Real Name", result.user["displayName"])
    }

    @Test fun firstSignInUsesTheIncomingName() {
        val result = patches(ExistingProfileDoc(exists = false), name = "Fresh")
        assertEquals("Fresh", result.user["displayName"])
        assertTrue(result.user.containsKey("createdAt"))
    }

    @Test fun existingProfilesAreNotRecreated() {
        assertFalse(patches(ExistingProfileDoc(exists = true, displayName = "x")).user.containsKey("createdAt"))
    }

    @Test fun lastPlayedIsNeverResetWhenTheDocumentHasNone() {
        val result = patches(ExistingProfileDoc(exists = true, displayName = "x"))
        assertFalse(result.leaderboard.containsKey("lastPlayedTitle"))
        assertFalse(result.publicProfile.containsKey("lastPlayedTitle"))
        assertFalse(result.publicProfile.containsKey("lastPlayedAtMs"))
    }

    @Test fun lastPlayedIsCarriedWhenPresent() {
        val result = patches(ExistingProfileDoc(exists = true, displayName = "x", lastPlayedTitle = "Persona", lastPlayedAtMs = 99L))
        assertEquals("Persona", result.leaderboard["lastPlayedTitle"])
        assertEquals(99L, result.publicProfile["lastPlayedAtMs"])
    }

    @Test fun photoIsKeptWhenTheProviderHasNoneAndRefreshedWhenItDoes() {
        assertEquals("https://old", patches(ExistingProfileDoc(exists = true, displayName = "x", photoURL = "https://old")).user["photoURL"])
        assertEquals("https://new", patches(ExistingProfileDoc(exists = true, displayName = "x", photoURL = "https://old"), photo = "https://new").user["photoURL"])
    }

    // B064

    @Test fun signInNeverMirrorsTheGamesMap() {
        val result = patches(ExistingProfileDoc(exists = true, displayName = "x", gamesMapSize = 500))
        assertFalse(result.user.containsKey("games"))
        assertEquals(500L, result.user["gamesPlayed"])
    }

    @Test fun gamesMapPatchKeepsOnlyTheTopGamesAndDeletesTheTail() {
        val marker = Any()
        val existing = (1..5).associate { "G$it" to mapOf<String, Any?>("ms" to (it * 100L)) }
        val changed = mapOf("G9" to mapOf<String, Any?>("ms" to 10_000L))
        val patch = ProfileDocLogic.gamesMapPatch(existing, changed, cap = 3, deleteMarker = marker)
        assertEquals(changed["G9"], patch["G9"])
        assertSame(marker, patch["G1"])
        assertSame(marker, patch["G2"])
        assertSame(marker, patch["G3"])
        assertFalse(patch.containsKey("G4"))
        assertFalse(patch.containsKey("G5"))
    }

    @Test fun gamesMapPatchDoesNotWriteAChangedGameThatFallsOutOfTheCap() {
        val marker = Any()
        val existing = (1..3).associate { "G$it" to mapOf<String, Any?>("ms" to (1_000L * it)) }
        val changed = mapOf("NEW" to mapOf<String, Any?>("ms" to 1L))
        val patch = ProfileDocLogic.gamesMapPatch(existing, changed, cap = 3, deleteMarker = marker)
        assertTrue(patch.isEmpty())
    }

    private fun assertSame(expected: Any, actual: Any?) = org.junit.Assert.assertSame(expected, actual)

    // B067 client side bounds

    @Test fun deltasAreBoundedPerUpload() {
        val day = 24L * 60 * 60 * 1000
        val entries = listOf(
            PlayerPlayTimeDelta("A", "A", null, 20 * day, sessionCount = 50_000L),
            PlayerPlayTimeDelta("B", "B", null, 20 * day, sessionCount = 1L),
            PlayerPlayTimeDelta("C", "C", null, day)
        )
        val bounded = ProfileDocLogic.boundDeltas(entries)
        assertEquals(ProfileDocLogic.MAX_BATCH_PLAY_TIME_MS, bounded.sumOf { it.durationMs })
        assertEquals(ProfileDocLogic.MAX_ENTRY_SESSIONS, bounded.first().sessionCount)
        assertEquals(2, bounded.size)
    }

    @Test fun invalidDeltasAreDropped() {
        val bounded = ProfileDocLogic.boundDeltas(listOf(
            PlayerPlayTimeDelta(" ", "x", null, 10L),
            PlayerPlayTimeDelta("A", "A", null, -5L),
            PlayerPlayTimeDelta("B", "B", null, 10L)
        ))
        assertEquals(listOf("B"), bounded.map { it.titleId })
    }

    @Test fun entriesOfTheSameGameAreMerged() {
        val grouped = ProfileDocLogic.groupByGame(listOf(
            PlayerPlayTimeDelta("pcse00001", "G", null, 10L, sessionCount = 1L, lastPlayedAtMs = 5L),
            PlayerPlayTimeDelta("PCSE00001", "G", null, 20L, sessionCount = 2L, lastPlayedAtMs = 9L)
        ))
        assertEquals(1, grouped.size)
        assertEquals(30L, grouped[0].second.durationMs)
        assertEquals(3L, grouped[0].second.sessionCount)
        assertEquals(9L, grouped[0].second.lastPlayedAtMs)
    }

    // B063 server-side idempotency memory

    @Test fun rememberedBatchesAreRecognisedAndBounded() {
        var ids = emptyList<String>()
        repeat(ProfileDocLogic.MAX_REMEMBERED_BATCH_IDS + 5) { ids = ProfileDocLogic.rememberBatch(ids, "b$it") }
        assertEquals(ProfileDocLogic.MAX_REMEMBERED_BATCH_IDS, ids.size)
        assertTrue(ProfileDocLogic.isBatchApplied(ids, "b${ProfileDocLogic.MAX_REMEMBERED_BATCH_IDS + 4}"))
        assertFalse(ProfileDocLogic.isBatchApplied(ids, "b0"))
        assertEquals(ids, ProfileDocLogic.rememberBatch(ids, ids.last()))
    }

    @Test fun nameResolutionPrefersStoredUnlessForced() {
        assertEquals("kept", ProfileDocLogic.resolveDisplayName("kept", "incoming", false))
        assertEquals("incoming", ProfileDocLogic.resolveDisplayName(" ", "incoming", false))
        assertEquals("incoming", ProfileDocLogic.resolveDisplayName(null, "incoming", false))
        assertEquals("incoming", ProfileDocLogic.resolveDisplayName("kept", "incoming", true))
        assertNull(null)
    }
}
