package com.sbro.emucorev.core.review

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ReviewPromptPolicyTest {
    @Test
    fun reviewRequiresOneFiveMinuteSession() {
        var progress = InAppReviewProgress()
        progress = InAppReviewPolicy.recordSession(progress, 5L * 60_000L - 1L)

        assertEquals(0, progress.qualifyingSessionCount)
        assertFalse(InAppReviewPolicy.canAttempt(progress, nowMs = 1_000L))

        progress = InAppReviewPolicy.recordSession(progress, 5L * 60_000L)

        assertEquals(1, progress.qualifyingSessionCount)
        assertTrue(progress.totalActivePlayTimeMs >= 5L * 60_000L)
        assertTrue(InAppReviewPolicy.canAttempt(progress, nowMs = 1_000L))
    }

    @Test
    fun shortSessionsAccumulatePlayTimeButDoNotQualifyAsMeaningfulSessions() {
        var progress = InAppReviewProgress()
        repeat(2) {
            progress = InAppReviewPolicy.recordSession(progress, 3L * 60_000L)
        }

        assertEquals(0, progress.qualifyingSessionCount)
        assertTrue(progress.totalActivePlayTimeMs >= InAppReviewPolicy.MIN_TOTAL_ACTIVE_PLAY_TIME_MS)
        assertFalse(InAppReviewPolicy.canAttempt(progress, nowMs = 1_000L))
    }

    @Test
    fun failedAttemptIsThrottledForOneDay() {
        val eligible = InAppReviewProgress(
            qualifyingSessionCount = InAppReviewPolicy.MIN_QUALIFYING_SESSION_COUNT,
            totalActivePlayTimeMs = InAppReviewPolicy.MIN_TOTAL_ACTIVE_PLAY_TIME_MS,
            lastAttemptAtMs = 10_000L
        )

        assertFalse(
            InAppReviewPolicy.canAttempt(
                eligible,
                nowMs = 10_000L + InAppReviewPolicy.RETRY_COOLDOWN_MS - 1L
            )
        )
        assertTrue(
            InAppReviewPolicy.canAttempt(
                eligible,
                nowMs = 10_000L + InAppReviewPolicy.RETRY_COOLDOWN_MS
            )
        )
    }

    @Test
    fun completedReviewPermanentlyDisablesFurtherAttempts() {
        val completed = InAppReviewProgress(
            qualifyingSessionCount = Int.MAX_VALUE,
            totalActivePlayTimeMs = Long.MAX_VALUE,
            reviewRequested = true
        )

        assertEquals(completed, InAppReviewPolicy.recordSession(completed, Long.MAX_VALUE))
        assertFalse(InAppReviewPolicy.canAttempt(completed, nowMs = Long.MAX_VALUE))
    }

    @Test
    fun countersSaturateWithoutOverflowAndClockRollbackCanRecover() {
        val saturated = InAppReviewPolicy.recordSession(
            InAppReviewProgress(
                qualifyingSessionCount = Int.MAX_VALUE,
                totalActivePlayTimeMs = Long.MAX_VALUE - 1L
            ),
            activePlayTimeMs = Long.MAX_VALUE
        )

        assertEquals(Int.MAX_VALUE, saturated.qualifyingSessionCount)
        assertEquals(Long.MAX_VALUE, saturated.totalActivePlayTimeMs)
        assertTrue(
            InAppReviewPolicy.canAttempt(
                saturated.copy(lastAttemptAtMs = 100_000L),
                nowMs = 50_000L
            )
        )
    }
}
