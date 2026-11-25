package com.sbro.emucorev.data

import android.content.Context
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

/** B062 (per-account queue) and B063 (idempotent peek -> commit -> remove upload). */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class ProfilePlayTimeSyncerTest {
    private lateinit var context: Context
    private lateinit var cache: PlayTimeSyncCacheRepository

    /** Mimics the server: a batch id that was already applied is acknowledged without counting again. */
    private class FakeBackend : PlayTimeBackend {
        var uid: String? = null
        val appliedBatches = LinkedHashSet<String>()
        val totalMs = HashMap<String, Long>()
        val attemptedIds = mutableListOf<String>()
        /** When set, the commit is applied on the server but the acknowledgement is lost. */
        var failAfterApplying = false
        var failBeforeApplying = false

        override fun currentUid(): String? = uid

        override suspend fun commitBatch(uid: String, batchId: String, entries: List<PlayerPlayTimeDelta>) {
            attemptedIds += batchId
            if (this.uid != uid) throw IllegalStateException("account changed")
            if (failBeforeApplying) throw java.io.IOException("offline")
            if (appliedBatches.add(batchId)) {
                totalMs[uid] = (totalMs[uid] ?: 0L) + entries.sumOf { it.durationMs }
            }
            if (failAfterApplying) throw java.io.IOException("ack lost")
        }
    }

    private lateinit var backend: FakeBackend
    private lateinit var syncer: ProfilePlayTimeSyncer

    @Before fun setUp() {
        context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("play_time_sync_cache", Context.MODE_PRIVATE).edit().clear().commit()
        cache = PlayTimeSyncCacheRepository(context)
        backend = FakeBackend()
        syncer = ProfilePlayTimeSyncer(cache, backend) { null }
    }

    @Test fun signedOutPlayTimeIsNotQueuedForWhoeverSignsInLater() = runBlocking {
        backend.uid = null
        syncer.recordSession("PCSE00001", "Game", 60_000L)
        backend.uid = "bob"
        syncer.syncPending()
        assertNull(cache.peekBatch("bob"))
        assertEquals(null, backend.totalMs["bob"])
    }

    @Test fun sessionsAreCreditedOnlyToTheAccountThatPlayedThem() = runBlocking {
        backend.uid = "alice"
        syncer.recordSession("PCSE00001", "Game", 60_000L)
        backend.uid = "bob"
        syncer.recordSession("PCSE00001", "Game", 5_000L)
        syncer.syncPending()
        assertEquals(5_000L, backend.totalMs["bob"])
        assertNull(backend.totalMs["alice"])
        // alice's time is still queued for alice
        backend.uid = "alice"
        syncer.syncPending()
        assertEquals(60_000L, backend.totalMs["alice"])
    }

    @Test fun successfulUploadRemovesTheBatch() = runBlocking {
        backend.uid = "alice"
        syncer.recordSession("PCSE00001", "Game", 1_000L)
        syncer.syncPending()
        assertEquals(1_000L, backend.totalMs["alice"])
        assertNull(cache.peekBatch("alice"))
    }

    @Test fun failedUploadKeepsTheBatchAndRetriesItUnderTheSameId() = runBlocking {
        backend.uid = "alice"
        backend.failBeforeApplying = true
        syncer.recordSession("PCSE00001", "Game", 1_000L)
        syncer.syncPending()
        assertEquals(null, backend.totalMs["alice"])
        backend.failBeforeApplying = false
        syncer.syncPending()
        assertEquals(1_000L, backend.totalMs["alice"])
        assertEquals(1, backend.attemptedIds.distinct().size)
        assertEquals(2, backend.attemptedIds.size)
    }

    @Test fun commitWithLostAcknowledgementIsNotCountedTwice() = runBlocking {
        backend.uid = "alice"
        backend.failAfterApplying = true
        syncer.recordSession("PCSE00001", "Game", 1_000L)
        syncer.syncPending() // server applied it, client saw a failure: batch must stay queued
        assertEquals(1_000L, backend.totalMs["alice"])
        assertNotNull(cache.peekBatch("alice"))
        backend.failAfterApplying = false
        syncer.syncPending() // same id again: server skips it
        assertEquals(1_000L, backend.totalMs["alice"])
        assertNull(cache.peekBatch("alice"))
    }

    @Test fun timePlayedWhileABatchIsInFlightIsNotLost() = runBlocking {
        backend.uid = "alice"
        backend.failBeforeApplying = true
        syncer.recordSession("PCSE00001", "Game", 1_000L)
        syncer.syncPending() // batch sealed, upload failed
        syncer.recordSession("PCSE00001", "Game", 2_000L) // arrives after the seal
        backend.failBeforeApplying = false
        syncer.syncPending()
        assertEquals(3_000L, backend.totalMs["alice"])
        assertEquals(2, backend.appliedBatches.size)
    }

    @Test fun uploadForAnotherSignedInAccountIsRefused() = runBlocking {
        backend.uid = "alice"
        syncer.recordSession("PCSE00001", "Game", 1_000L)
        val batch = cache.peekBatch("alice")!!
        backend.uid = "bob"
        val failed = runCatchingCancellable { backend.commitBatch("alice", batch.id, batch.entries) }.isFailure
        assertTrue(failed)
        assertEquals(null, backend.totalMs["bob"])
    }

    @Test fun legacyEntriesWithoutAnOwnerAreAdoptedByTheFirstAccountToSync() = runBlocking {
        cache.add(PlayerPlayTimeDelta(titleId = "PCSE00001", title = "Game", coverArtPath = null, durationMs = 4_000L, uid = ""))
        val batch = cache.peekBatch("alice")
        assertNotNull(batch)
        assertEquals("alice", batch!!.uid)
        assertEquals(listOf("alice"), batch.entries.map { it.uid })
        assertNull(cache.peekBatch("bob"))
    }

    @Test fun peekIsStableUntilRemoved() = runBlocking {
        cache.add(PlayerPlayTimeDelta("PCSE00001", "Game", null, 1_000L, uid = "alice"))
        val first = cache.peekBatch("alice")!!
        cache.add(PlayerPlayTimeDelta("PCSE00001", "Game", null, 9_000L, uid = "alice"))
        val second = cache.peekBatch("alice")!!
        assertEquals(first.id, second.id)
        assertEquals(1_000L, second.entries.sumOf { it.durationMs })
        cache.removeBatch(first.id)
        val next = cache.peekBatch("alice")!!
        assertNotEquals(first.id, next.id)
        assertEquals(9_000L, next.entries.sumOf { it.durationMs })
    }

    @Test fun discardAccountDropsOnlyThatAccountsQueue() = runBlocking {
        cache.add(PlayerPlayTimeDelta("PCSE00001", "Game", null, 1_000L, uid = "alice"))
        cache.add(PlayerPlayTimeDelta("PCSE00002", "Other", null, 2_000L, uid = "bob"))
        cache.peekBatch("alice") // sealed
        cache.discardAccount("alice")
        assertNull(cache.peekBatch("alice"))
        assertEquals(2_000L, cache.peekBatch("bob")!!.entries.sumOf { it.durationMs })
    }
}
