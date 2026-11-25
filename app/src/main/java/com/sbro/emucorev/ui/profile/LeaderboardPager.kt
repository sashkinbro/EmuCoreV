package com.sbro.emucorev.ui.profile

import com.google.firebase.firestore.DocumentSnapshot
import com.sbro.emucorev.data.PlayerLeaderboardEntry
import com.sbro.emucorev.data.PlayerLeaderboardPage
import com.sbro.emucorev.data.ProfileFeedEvent
import com.sbro.emucorev.data.ProfileFriendship
import com.sbro.emucorev.data.runCatchingCancellable

/**
 * Cursor paging of the leaderboard (B070). Every call is tied to a generation: a [reset] or
 * [invalidate] makes responses of earlier, still running requests stale, so they can neither append
 * to nor corrupt the new list. Ranks come from the number of server rows already consumed, not from
 * the size of any filtered list. Intended for a single (main) thread.
 */
class LeaderboardPager(
    private val loadPage: suspend (cursor: DocumentSnapshot?, rankOffset: Int) -> PlayerLeaderboardPage
) {
    sealed interface Outcome {
        /** [entries] are the new rows only; [replace] says the list restarts with them. */
        data class Loaded(val entries: List<PlayerLeaderboardEntry>, val hasMore: Boolean, val replace: Boolean) : Outcome
        data class Failed(val error: Throwable, val replace: Boolean) : Outcome

        /** The request was superseded or is not allowed right now; apply nothing. */
        data object Ignored : Outcome
    }

    private var generation = 0
    private var cursor: DocumentSnapshot? = null
    private var consumedRows = 0
    private var inFlight = false
    private var hasMore = true

    fun invalidate() {
        generation++
        cursor = null
        consumedRows = 0
        inFlight = false
        hasMore = true
    }

    suspend fun reset(): Outcome {
        invalidate()
        return request(replace = true)
    }

    suspend fun loadMore(): Outcome {
        if (inFlight || !hasMore) return Outcome.Ignored
        return request(replace = false)
    }

    private suspend fun request(replace: Boolean): Outcome {
        val ticket = generation
        inFlight = true
        val result = try {
            runCatchingCancellable { loadPage(cursor, consumedRows) }
        } finally {
            if (ticket == generation) inFlight = false
        }
        if (ticket != generation) return Outcome.Ignored
        val page = result.getOrElse { return Outcome.Failed(it, replace) }
        cursor = page.cursor
        consumedRows += page.entries.size
        hasMore = page.hasMore
        return Outcome.Loaded(page.entries, page.hasMore, replace)
    }
}

/** Entries of players the signed-in user blocked must not show up in lists (B069). */
internal fun List<PlayerLeaderboardEntry>.excludingBlocked(blocked: Collection<String>): List<PlayerLeaderboardEntry> {
    if (blocked.isEmpty()) return this
    val set = blocked.toSet()
    return filterNot { it.uid in set }
}

internal fun List<ProfileFeedEvent>.excludingBlockedAuthors(blocked: Collection<String>): List<ProfileFeedEvent> {
    if (blocked.isEmpty()) return this
    val set = blocked.toSet()
    return filterNot { it.uid in set }
}

internal fun List<ProfileFriendship>.excludingBlockedFriends(blocked: Collection<String>): List<ProfileFriendship> {
    if (blocked.isEmpty()) return this
    val set = blocked.toSet()
    return filterNot { it.otherUid in set }
}
