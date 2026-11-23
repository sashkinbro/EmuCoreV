package com.sbro.emucorev.core

import android.app.Activity
import android.util.Log
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import com.google.android.gms.tasks.Task
import com.google.android.play.agesignals.AgeSignalsAccessRequest
import com.google.android.play.agesignals.AgeSignalsException
import com.google.android.play.agesignals.AgeSignalsManager
import com.google.android.play.agesignals.AgeSignalsManagerFactory
import com.google.android.play.agesignals.AgeSignalsRequest
import com.google.android.play.agesignals.ApplicableAgeFeaturesRequest
import com.google.android.play.agesignals.model.ApplicableAgeFeature
import com.sbro.emucorev.data.AppPreferences
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.suspendCancellableCoroutine
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

/**
 * Google Play Age Signals integration (library com.google.android.play:age-signals).
 *
 * Why: US state app-store age laws (Texas SB2420, Utah, Louisiana) and Brazil's ECA Digital
 * require apps distributed through Play to react to the user's declared age range; Play may
 * restrict distribution in those regions otherwise. Outside regulated regions every field is
 * null and nothing here has any effect.
 *
 * Flow (once per process, from MainActivity after the first frame, never blocking startup):
 *  1. getApplicableAgeFeatures(); if it contains MANDATORY_SHARING, call requestAgeSignalsAccess
 *     with a RESUMED Activity (Play shows its own consent prompt).
 *  2. checkAgeSignals() and map the result into [AgeSignalsSnapshot] (SDK types never leave this file).
 *  3. Persist the snapshot (non-cloud DataStore) and publish it on [ageSignals].
 * Transient errors (network / bind / client transient) are retried at most 3 times with backoff.
 * Every failure is silent (logged only). Age signals must never gate app start, purchases or Pro
 * features; they only drive conservative defaults for optional data collection.
 *
 * To port to another app: copy this file, the age-signal block in AppPreferences, the
 * MainActivity launch and the gradle dependency.
 */
object AgeSignalsController {
    private const val TAG = "AgeSignals"

    private val started = AtomicBoolean(false)
    private val state = MutableStateFlow(AgeSignalsSnapshot.EMPTY)

    /** Latest known result; seeded from disk by [publishStored] and refreshed by [run]. */
    val ageSignals: StateFlow<AgeSignalsSnapshot> = state.asStateFlow()

    /** Loads the last persisted snapshot so consumers have a value before the Play call returns. */
    suspend fun publishStored(preferences: AppPreferences) {
        val stored = runCatching { preferences.getAgeSignals() }.getOrNull() ?: return
        state.value = stored
        AppAnalytics.applyAgeSignals(stored)
    }

    /** Runs at most once per process. Safe to call from a lifecycleScope; never throws. */
    suspend fun run(activity: Activity, preferences: AppPreferences, isDebugBuild: Boolean) {
        if (!started.compareAndSet(false, true)) return
        try {
            val manager = AgeSignalsManagerFactory.create(activity.applicationContext)
            val snapshot = fetchWithRetry(manager, activity)
            state.value = snapshot
            AppAnalytics.applyAgeSignals(snapshot)
            preferences.setAgeSignals(snapshot)
            if (snapshot.consentDeclined) {
                Log.i(TAG, "Significant-change consent declined; no action taken")
            }
        } catch (cancelled: CancellationException) {
            started.set(false)
            throw cancelled
        } catch (error: Throwable) {
            val code = (error as? AgeSignalsException)?.errorCode
            if (code == AgeSignalsErrors.APP_NOT_OWNED && isDebugBuild) {
                Log.i(TAG, "Age signals unavailable (APP_NOT_OWNED, sideloaded debug build)")
            } else {
                Log.w(TAG, "Age signals unavailable (code=$code)", error)
            }
        }
    }

    private suspend fun fetchWithRetry(manager: AgeSignalsManager, activity: Activity): AgeSignalsSnapshot {
        var attempt = 0
        while (true) {
            try {
                return fetchOnce(manager, activity)
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (error: Throwable) {
                val code = (error as? AgeSignalsException)?.errorCode ?: throw error
                val delayMs = AgeSignalsErrors.retryDelayMs(code, attempt) ?: throw error
                attempt++
                delay(delayMs)
            }
        }
    }

    private suspend fun fetchOnce(manager: AgeSignalsManager, activity: Activity): AgeSignalsSnapshot {
        val features = manager.getApplicableAgeFeatures(ApplicableAgeFeaturesRequest.builder().build())
            .await().features()
        if (ApplicableAgeFeature.MANDATORY_SHARING in features && activity.isResumedForPrompt()) {
            manager.requestAgeSignalsAccess(
                AgeSignalsAccessRequest.builder().setActivity(activity).build()
            ).await()
        }
        val result = manager.checkAgeSignals(AgeSignalsRequest.builder().build()).await()
        return AgeSignalsSnapshot(
            ageLower = result.ageLower(),
            ageUpper = result.ageUpper(),
            significantChangeStatus = result.significantChangeStatus(),
            installId = result.installId(),
            ageRangeSource = result.ageRangeSource(),
            timestampMs = System.currentTimeMillis()
        )
    }

    private fun Activity.isResumedForPrompt(): Boolean =
        (this as? LifecycleOwner)?.lifecycle?.currentState?.isAtLeast(Lifecycle.State.RESUMED) == true &&
            !isFinishing

    private suspend fun <T> Task<T>.await(): T = suspendCancellableCoroutine { cont ->
        addOnSuccessListener { if (cont.isActive) cont.resume(it) }
        addOnFailureListener { if (cont.isActive) cont.resumeWithException(it) }
        addOnCanceledListener { cont.cancel() }
    }
}

/** SDK-independent copy of AgeSignalsResult. Null fields mean "outside regulated region / not shared". */
data class AgeSignalsSnapshot(
    val ageLower: Int? = null,
    /** Null for 18+ or unknown. */
    val ageUpper: Int? = null,
    /** SignificantChangeStatus constants: 0 unspecified, 1 approved, 2 pending, 3 declined. */
    val significantChangeStatus: Int? = null,
    val installId: String? = null,
    /** AgeRangeSource constants: 0 unspecified, 1..4 = TIER_A..TIER_D. */
    val ageRangeSource: Int? = null,
    val timestampMs: Long = 0L
) {
    val isMinor: Boolean get() = ageUpper != null && ageUpper < 18
    val consentDeclined: Boolean get() = significantChangeStatus == AgeSignalsErrors.STATUS_DECLINED

    companion object {
        val EMPTY = AgeSignalsSnapshot()
    }
}

/** Pure error-code policy (AgeSignalsException.errorCode values). */
object AgeSignalsErrors {
    const val NETWORK_ERROR = -3
    const val CANNOT_BIND_TO_SERVICE = -5
    const val CLIENT_TRANSIENT_ERROR = -8
    const val APP_NOT_OWNED = -9
    const val STATUS_DECLINED = 3
    const val MAX_RETRIES = 3

    fun isRetryable(code: Int): Boolean =
        code == NETWORK_ERROR || code == CANNOT_BIND_TO_SERVICE || code == CLIENT_TRANSIENT_ERROR

    /** Delay before retry number [attempt] (0-based), or null when the error is final / retries exhausted. */
    fun retryDelayMs(code: Int, attempt: Int): Long? =
        if (isRetryable(code) && attempt < MAX_RETRIES) 1_000L shl attempt else null
}
