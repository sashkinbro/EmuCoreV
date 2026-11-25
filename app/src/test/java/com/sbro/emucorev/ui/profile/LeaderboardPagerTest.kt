package com.sbro.emucorev.ui.profile

import com.sbro.emucorev.data.PlayerLeaderboardEntry
import com.sbro.emucorev.data.PlayerLeaderboardPage
import com.sbro.emucorev.data.ProfileFeedEvent
import com.sbro.emucorev.data.ProfileFriendship
import com.sbro.emucorev.data.FriendshipStatus
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class LeaderboardPagerTest {
    private fun entry(uid: String, rank: Int) = PlayerLeaderboardEntry(
        rank = rank, uid = uid, displayName = uid, photoURL = null, totalPlayTimeMs = 0L, gamesPlayed = 0,
        lastPlayedTitle = null, playerTag = "EV-$uid", profileAccent = "gold", isProMember = false
    )

    private fun page(vararg uids: String, offset: Int = 0, hasMore: Boolean = true) = PlayerLeaderboardPage(
        entries = uids.mapIndexed { i, uid -> entry(uid, offset + i + 1) }, cursor = null, hasMore = hasMore
    )

    @Test fun ranksContinueFromTheRowsAlreadyConsumed() = runBlocking {
        val offsets = mutableListOf<Int>()
        val pager = LeaderboardPager { _, offset -> offsets += offset; page("a", "b", "c", offset = offset) }
        pager.reset()
        pager.loadMore()
        assertEquals(listOf(0, 3), offsets)
    }

    @Test fun aResponseThatArrivesAfterAResetIsDiscarded() = runBlocking {
        val first = CompletableDeferred<PlayerLeaderboardPage>()
        val second = CompletableDeferred<PlayerLeaderboardPage>()
        val queue = ArrayDeque(listOf(first, second))
        val pager = LeaderboardPager { _, _ -> queue.removeFirst().await() }
        val stale = async(start = CoroutineStart.UNDISPATCHED) { pager.reset() }
        val fresh = async(start = CoroutineStart.UNDISPATCHED) { pager.reset() }
        second.complete(page("new"))
        first.complete(page("old"))
        assertTrue(stale.await() is LeaderboardPager.Outcome.Ignored)
        val loaded = fresh.await() as LeaderboardPager.Outcome.Loaded
        assertEquals(listOf("new"), loaded.entries.map { it.uid })
        assertTrue(loaded.replace)
    }

    @Test fun loadMoreIsSingleFlight() = runBlocking {
        val gate = CompletableDeferred<PlayerLeaderboardPage>()
        var calls = 0
        val pager = LeaderboardPager { _, _ -> calls++; if (calls == 1) page("a") else gate.await() }
        pager.reset()
        val running = async(start = CoroutineStart.UNDISPATCHED) { pager.loadMore() }
        val duplicate = pager.loadMore()
        assertTrue(duplicate is LeaderboardPager.Outcome.Ignored)
        gate.complete(page("b", offset = 1, hasMore = false))
        assertTrue(running.await() is LeaderboardPager.Outcome.Loaded)
        assertEquals(2, calls)
    }

    @Test fun loadMoreStopsWhenThereAreNoMorePages() = runBlocking {
        val pager = LeaderboardPager { _, _ -> page("a", hasMore = false) }
        pager.reset()
        assertTrue(pager.loadMore() is LeaderboardPager.Outcome.Ignored)
    }

    @Test fun aFailedFirstPageIsReportedAsAReplaceFailureAndCanBeRetried() = runBlocking {
        var attempt = 0
        val pager = LeaderboardPager { _, _ -> if (attempt++ == 0) throw java.io.IOException("offline") else page("a") }
        val failed = pager.reset()
        assertTrue(failed is LeaderboardPager.Outcome.Failed && failed.replace)
        val retried = pager.reset()
        assertTrue(retried is LeaderboardPager.Outcome.Loaded)
    }

    // B069

    @Test fun blockedPlayersAreRemovedFromLeaderboardFeedAndFriends() {
        val entries = listOf(entry("a", 1), entry("blocked", 2), entry("c", 3))
        assertEquals(listOf("a", "c"), entries.excludingBlocked(listOf("blocked")).map { it.uid })
        assertEquals(entries, entries.excludingBlocked(emptyList()))

        val feed = listOf(ProfileFeedEvent("1", "a", "t", null, null, null), ProfileFeedEvent("2", "blocked", "t", null, null, null))
        assertEquals(listOf("1"), feed.excludingBlockedAuthors(listOf("blocked")).map { it.id })

        val friends = listOf(
            ProfileFriendship("x", "a", FriendshipStatus.Accepted, null),
            ProfileFriendship("y", "blocked", FriendshipStatus.PendingIncoming, null)
        )
        assertEquals(listOf("x"), friends.excludingBlockedFriends(listOf("blocked")).map { it.id })
    }
}
