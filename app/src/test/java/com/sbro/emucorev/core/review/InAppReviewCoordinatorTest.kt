package com.sbro.emucorev.core.review

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class InAppReviewCoordinatorTest {
    private val now = 200L * DAY_MS
    private val eligibleUsage = InAppReviewProgress(
        qualifyingSessionCount = 1,
        totalActivePlayTimeMs = 5L * 60_000L
    )

    @Test
    fun eligibleAttemptClaimsOnceAndStartsPlayRequest() {
        val store = FakeStore()
        val client = FakeClient()
        val coordinator = coordinator(store, client)

        assertTrue(coordinator.attempt(now))
        assertEquals(now, store.state.lastAttemptAtMs)
        assertEquals(1, client.requestCount)
        assertFalse(coordinator.attempt(now))
        assertEquals(1, client.requestCount)
    }

    @Test
    fun completedFlowPermanentlyMarksReviewRequested() {
        val store = FakeStore()
        val client = FakeClient()
        val coordinator = coordinator(store, client)

        assertTrue(coordinator.attempt(now))
        client.complete(InAppReviewResult.COMPLETED)

        assertTrue(store.state.reviewRequested)
        assertFalse(coordinator.attempt(now + 365L * DAY_MS))
        assertEquals(1, client.requestCount)
    }

    @Test
    fun retryableFailureKeepsOneDayCooldown() {
        val store = FakeStore()
        val client = FakeClient()
        val coordinator = coordinator(store, client)

        assertTrue(coordinator.attempt(now))
        client.complete(InAppReviewResult.RETRYABLE_FAILURE)

        assertFalse(store.state.reviewRequested)
        assertFalse(coordinator.attempt(now + DAY_MS - 1L))
        assertTrue(coordinator.attempt(now + DAY_MS))
        assertEquals(2, client.requestCount)
    }

    @Test
    fun activityNotReadyReleasesClaimForImmediateRetry() {
        val store = FakeStore()
        val client = FakeClient()
        val coordinator = coordinator(store, client)

        assertTrue(coordinator.attempt(now))
        client.complete(InAppReviewResult.ACTIVITY_NOT_READY)

        assertEquals(0L, store.state.lastAttemptAtMs)
        assertTrue(coordinator.attempt(now))
    }

    @Test
    fun insufficientUsageOrNonPlayInstallDoesNotClaimAttempt() {
        val store = FakeStore()
        val client = FakeClient()
        val insufficient = InAppReviewCoordinator(
            progressSource = FakeProgressSource(eligibleUsage.copy(qualifyingSessionCount = 0)),
            store = store,
            client = client
        )

        assertFalse(insufficient.attempt(now))
        assertEquals(0L, store.state.lastAttemptAtMs)

        client.requestAllowed = false
        val unavailable = coordinator(store, client)
        assertFalse(unavailable.attempt(now))
        assertEquals(0, client.requestCount)
    }

    private fun coordinator(store: FakeStore, client: FakeClient) = InAppReviewCoordinator(
        progressSource = FakeProgressSource(eligibleUsage),
        store = store,
        client = client
    )

    private class FakeProgressSource(
        private val progress: InAppReviewProgress
    ) : InAppReviewProgressSource {
        override fun readUsage(): InAppReviewProgress = progress
    }

    private class FakeStore : InAppReviewAttemptStore {
        var state = InAppReviewProgress()

        override fun readAttemptState(): InAppReviewProgress = state

        override fun claimAttempt(atMs: Long) {
            state = state.copy(lastAttemptAtMs = atMs)
        }

        override fun releaseAttempt(claimedAtMs: Long) {
            if (state.lastAttemptAtMs == claimedAtMs && !state.reviewRequested) {
                state = state.copy(lastAttemptAtMs = 0L)
            }
        }

        override fun markRequested(claimedAtMs: Long) {
            if (state.lastAttemptAtMs == claimedAtMs) {
                state = state.copy(reviewRequested = true)
            }
        }
    }

    private class FakeClient : InAppReviewClient {
        var requestAllowed = true
        var requestCount = 0
        private var callback: ((InAppReviewResult) -> Unit)? = null

        override fun canRequest(): Boolean = requestAllowed

        override fun request(onComplete: (InAppReviewResult) -> Unit) {
            requestCount += 1
            callback = onComplete
        }

        fun complete(result: InAppReviewResult) {
            val pending = checkNotNull(callback)
            callback = null
            pending(result)
        }
    }

    private companion object {
        const val DAY_MS = 24L * 60L * 60L * 1_000L
    }
}
