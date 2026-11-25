package com.sbro.emucorev.data

import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class AccountDataEraserTest {
    private class FakeStore(documents: Collection<String>) : AccountDataStore {
        val live = LinkedHashSet(documents)
        val deleteCalls = mutableListOf<List<String>>()

        override suspend fun listDocumentPaths(collectionPath: String, limit: Int): List<String> =
            live.filter { it.startsWith("$collectionPath/") && !it.removePrefix("$collectionPath/").contains('/') }
                .take(limit)

        override suspend fun listFriendshipPaths(uid: String, limit: Int): List<String> =
            live.filter { it.startsWith("friendships/") && uid in it.removePrefix("friendships/").split("_") }.take(limit)

        override suspend fun deleteDocuments(paths: List<String>) {
            deleteCalls += paths
            live.removeAll(paths.toSet())
        }
    }

    private val uid = "u1"

    private fun accountDocs() = listOf(
        "users/u1", "publicProfiles/u1", "leaderboardPlayTime/u1", "profile_backups/u1",
        "users/u1/activity/2026-11-24", "users/u1/activity/2026-11-25", "users/u1/blocks/u9",
        "users/u1/devices/d1", "users/u1/emulatorProfiles/p1", "users/u1/trophyBackups/archive",
        "publicProfiles/u1/games/PCSE00001", "publicProfiles/u1/feed/e1",
        "friendships/u1_u2", "friendships/u0_u1"
    )

    @Test fun removesEveryDocumentOfTheAccountAndNothingElse() = runBlocking {
        val others = listOf("users/u2", "publicProfiles/u2", "users/u2/activity/x", "friendships/u2_u3", "leaderboardPlayTime/u2")
        val store = FakeStore(accountDocs() + others)
        AccountDataEraser(store).erase(uid)
        assertEquals(others.toSet(), store.live)
    }

    @Test fun subcollectionsGoBeforeTheirParentDocuments() = runBlocking {
        val store = FakeStore(accountDocs())
        AccountDataEraser(store).erase(uid)
        val order = store.deleteCalls.flatten()
        assertTrue(order.indexOf("users/u1/activity/2026-11-25") < order.indexOf("users/u1"))
        assertTrue(order.indexOf("publicProfiles/u1/games/PCSE00001") < order.indexOf("publicProfiles/u1"))
        assertEquals("users/u1", order.last())
    }

    @Test fun largeCollectionsAreDeletedInBoundedPages() = runBlocking {
        val many = (1..(AccountDataEraser.BATCH_SIZE * 2 + 5)).map { "publicProfiles/u1/games/G$it" }
        val store = FakeStore(many + "users/u1")
        AccountDataEraser(store).erase(uid)
        assertTrue(store.live.isEmpty())
        assertTrue(store.deleteCalls.all { it.size <= AccountDataEraser.BATCH_SIZE })
    }

    @Test fun eraseIsIdempotent() = runBlocking {
        val store = FakeStore(accountDocs())
        val eraser = AccountDataEraser(store)
        eraser.erase(uid)
        eraser.erase(uid)
        assertTrue(store.live.isEmpty())
    }

    @Test fun blankUidIsRejected() {
        try {
            runBlocking { AccountDataEraser(FakeStore(emptyList())).erase(" ") }
            fail("blank uid must be rejected")
        } catch (_: IllegalArgumentException) {
        }
    }

    @Test fun reauthMethodFollowsTheLinkedProviders() {
        assertEquals(ReauthMethod.Password, reauthMethodFor(listOf("firebase", "password")))
        assertEquals(ReauthMethod.Google, reauthMethodFor(listOf("firebase", "google.com")))
        assertEquals(ReauthMethod.Password, reauthMethodFor(listOf("google.com", "password")))
        assertEquals(ReauthMethod.Unsupported, reauthMethodFor(listOf("firebase", "github.com")))
        assertEquals(ReauthMethod.Unsupported, reauthMethodFor(emptyList()))
    }
}
