package com.sbro.emucorev.core.review

import android.content.Context
import android.content.pm.ApplicationInfo
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.core.content.edit
import androidx.lifecycle.Lifecycle
import com.google.android.play.core.review.ReviewManagerFactory
import com.sbro.emucorev.core.PlayTimeRepository
import java.util.concurrent.atomic.AtomicBoolean

class PlayTimeReviewProgressSource(context: Context) : InAppReviewProgressSource {
    private val appContext = context.applicationContext

    override fun readUsage(): InAppReviewProgress = PlayTimeRepository(appContext)
        .loadSessions()
        .asSequence()
        .filter { it.endedAt != null }
        .fold(InAppReviewProgress()) { progress, session ->
            InAppReviewPolicy.recordSession(progress, session.durationMs)
        }
}

class AndroidInAppReviewAttemptStore(context: Context) : InAppReviewAttemptStore {
    private val preferences = context.applicationContext.getSharedPreferences(
        PREFERENCES_NAME,
        Context.MODE_PRIVATE
    )

    override fun readAttemptState(): InAppReviewProgress = InAppReviewProgress(
        reviewRequested = preferences.getBoolean(KEY_REVIEW_REQUESTED, false),
        lastAttemptAtMs = preferences.getLong(KEY_LAST_ATTEMPT_AT, 0L)
    )

    override fun claimAttempt(atMs: Long) {
        preferences.edit { putLong(KEY_LAST_ATTEMPT_AT, atMs.coerceAtLeast(1L)) }
    }

    override fun releaseAttempt(claimedAtMs: Long) {
        if (preferences.getLong(KEY_LAST_ATTEMPT_AT, 0L) != claimedAtMs) return
        if (preferences.getBoolean(KEY_REVIEW_REQUESTED, false)) return
        preferences.edit { remove(KEY_LAST_ATTEMPT_AT) }
    }

    override fun markRequested(claimedAtMs: Long) {
        if (preferences.getLong(KEY_LAST_ATTEMPT_AT, 0L) != claimedAtMs) return
        preferences.edit { putBoolean(KEY_REVIEW_REQUESTED, true) }
    }

    private companion object {
        const val PREFERENCES_NAME = "in_app_review"
        const val KEY_LAST_ATTEMPT_AT = "last_attempt_at"
        const val KEY_REVIEW_REQUESTED = "review_requested"
    }
}

class GooglePlayInAppReviewClient(
    private val activity: ComponentActivity
) : InAppReviewClient {
    private val mainHandler = Handler(Looper.getMainLooper())

    override fun canRequest(): Boolean {
        if (activity.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0) return false

        val installerPackage = runCatching {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                activity.packageManager
                    .getInstallSourceInfo(activity.packageName)
                    .installingPackageName
            } else {
                @Suppress("DEPRECATION")
                activity.packageManager.getInstallerPackageName(activity.packageName)
            }
        }.getOrNull()
        return installerPackage == PLAY_STORE_PACKAGE
    }

    override fun request(onComplete: (InAppReviewResult) -> Unit) {
        val completionDelivered = AtomicBoolean(false)
        fun complete(result: InAppReviewResult) {
            if (!completionDelivered.compareAndSet(false, true)) return
            if (Looper.myLooper() == Looper.getMainLooper()) {
                onComplete(result)
            } else {
                mainHandler.post { onComplete(result) }
            }
        }

        if (!activity.isReadyForReview()) {
            complete(InAppReviewResult.ACTIVITY_NOT_READY)
            return
        }

        val reviewManager = runCatching {
            ReviewManagerFactory.create(activity.applicationContext)
        }.getOrElse { error ->
            Log.w(TAG, "Unable to create ReviewManager", error)
            complete(InAppReviewResult.RETRYABLE_FAILURE)
            return
        }
        val requestTask = runCatching { reviewManager.requestReviewFlow() }.getOrElse { error ->
            Log.w(TAG, "Unable to request review flow", error)
            complete(InAppReviewResult.RETRYABLE_FAILURE)
            return
        }

        requestTask.addOnCompleteListener { task ->
            if (!task.isSuccessful) {
                Log.w(TAG, "Review flow request failed", task.exception)
                complete(InAppReviewResult.RETRYABLE_FAILURE)
                return@addOnCompleteListener
            }
            if (!activity.isReadyForReview()) {
                complete(InAppReviewResult.ACTIVITY_NOT_READY)
                return@addOnCompleteListener
            }

            val reviewInfo = runCatching { task.result }.getOrElse { error ->
                Log.w(TAG, "ReviewInfo was unavailable", error)
                complete(InAppReviewResult.RETRYABLE_FAILURE)
                return@addOnCompleteListener
            }
            val launchTask = runCatching {
                reviewManager.launchReviewFlow(activity, reviewInfo)
            }.getOrElse { error ->
                Log.w(TAG, "Unable to launch review flow", error)
                complete(InAppReviewResult.RETRYABLE_FAILURE)
                return@addOnCompleteListener
            }
            launchTask.addOnCompleteListener { completedTask ->
                if (completedTask.isSuccessful) {
                    complete(InAppReviewResult.COMPLETED)
                } else {
                    Log.w(TAG, "Review flow launch failed", completedTask.exception)
                    complete(InAppReviewResult.RETRYABLE_FAILURE)
                }
            }
        }
    }

    private fun ComponentActivity.isReadyForReview(): Boolean =
        !isFinishing &&
            !isDestroyed &&
            lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)

    private companion object {
        const val TAG = "PlayInAppReview"
        const val PLAY_STORE_PACKAGE = "com.android.vending"
    }
}
