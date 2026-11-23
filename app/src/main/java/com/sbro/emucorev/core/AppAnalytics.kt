package com.sbro.emucorev.core

import android.content.Context
import android.os.Bundle
import android.util.Log
import com.google.firebase.analytics.FirebaseAnalytics
import com.sbro.emucorev.BuildConfig
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Centralized, best-effort product analytics.
 *
 * Event methods intentionally accept only bounded technical values. Never add game titles,
 * serials, file paths, account identifiers, or other user-provided strings here.
 */
object AppAnalytics {
    private const val TAG = "AppAnalytics"

    private val ready = AtomicBoolean(false)

    @Volatile
    private var firebaseAnalytics: FirebaseAnalytics? = null

    fun initialize(context: Context) {
        if (ready.get()) return

        runCatching {
            val analytics = FirebaseAnalytics.getInstance(context.applicationContext)
            analytics.setConsent(
                mapOf(
                    FirebaseAnalytics.ConsentType.ANALYTICS_STORAGE to
                        FirebaseAnalytics.ConsentStatus.GRANTED,
                    FirebaseAnalytics.ConsentType.AD_STORAGE to
                        FirebaseAnalytics.ConsentStatus.DENIED,
                    FirebaseAnalytics.ConsentType.AD_USER_DATA to
                        FirebaseAnalytics.ConsentStatus.DENIED,
                    FirebaseAnalytics.ConsentType.AD_PERSONALIZATION to
                        FirebaseAnalytics.ConsentStatus.DENIED
                )
            )
            analytics.setAnalyticsCollectionEnabled(true)
            analytics.setUserProperty(
                "build_type",
                if (BuildConfig.DEBUG) "debug" else "release"
            )
            firebaseAnalytics = analytics
            ready.set(true)
        }.onFailure { error ->
            Log.w(TAG, "Firebase Analytics initialization failed", error)
        }
    }

    /**
     * Minors (Play Age Signals) get analytics storage denied; ads consent is already denied.
     * Billing is unaffected by Analytics consent.
     */
    fun applyAgeSignals(snapshot: AgeSignalsSnapshot) {
        if (!snapshot.isMinor) return
        val analytics = firebaseAnalytics ?: return
        runCatching {
            analytics.setConsent(
                mapOf(
                    FirebaseAnalytics.ConsentType.ANALYTICS_STORAGE to
                        FirebaseAnalytics.ConsentStatus.DENIED
                )
            )
        }.onFailure { Log.w(TAG, "Failed to apply minor analytics consent", it) }
    }

    fun logOnboardingCompleted() {
        logEvent("onboarding_complete") {
            putString("performance_profile", AnalyticsDimensions.PROFILE_DEFAULT)
        }
    }

    fun logEmulationStarted(
        launchType: String,
        renderer: String,
        audioBackend: String,
        upscaleMultiplier: Float,
        saveStateLoad: Boolean
    ) {
        logEvent("emulation_start") {
            putString("launch_type", AnalyticsDimensions.sanitizeLaunchType(launchType))
            putString("renderer", AnalyticsDimensions.renderer(renderer))
            putString("audio_backend", AnalyticsDimensions.audioBackend(audioBackend))
            putString("upscale", AnalyticsDimensions.upscale(upscaleMultiplier))
            putString("performance_profile", AnalyticsDimensions.PROFILE_DEFAULT)
            putLong("save_state_load", saveStateLoad.toAnalyticsLong())
        }
    }

    fun logEmulationStartFailed(launchType: String, reason: String) {
        logEvent("emulation_start_failed") {
            putString("launch_type", AnalyticsDimensions.sanitizeLaunchType(launchType))
            putString("reason", AnalyticsDimensions.sanitizeFailureReason(reason))
        }
    }

    fun logEmulationEnded(activePlayTimeMs: Long, renderer: String, audioBackend: String) {
        val safeDurationMs = activePlayTimeMs.coerceAtLeast(0L)
        logEvent("emulation_end") {
            putLong("duration_seconds", safeDurationMs / 1_000L)
            putString("duration_bucket", AnalyticsDimensions.durationBucket(safeDurationMs))
            putString("renderer", AnalyticsDimensions.renderer(renderer))
            putString("audio_backend", AnalyticsDimensions.audioBackend(audioBackend))
        }
    }

    fun logSaveStateAction(action: String, automatic: Boolean, success: Boolean) {
        logEvent("save_state_action") {
            putString("action", AnalyticsDimensions.sanitizeSaveStateAction(action))
            putLong("automatic", automatic.toAnalyticsLong())
            putLong("success", success.toAnalyticsLong())
        }
    }

    fun logProPromoImpression(placement: String) {
        logEvent("pro_promo_impression") {
            putString("placement", ProAnalyticsDimensions.placement(placement))
        }
    }

    fun logProPromoAction(placement: String, action: String, dismissStreak: Int) {
        logEvent("pro_promo_action") {
            putString("placement", ProAnalyticsDimensions.placement(placement))
            putString("action", ProAnalyticsDimensions.promoAction(action))
            putLong("dismiss_streak", ProAnalyticsDimensions.streak(dismissStreak))
        }
    }

    fun logProReminderShown(dismissStreak: Int) {
        logEvent("pro_reminder_shown") {
            putLong("dismiss_streak", ProAnalyticsDimensions.streak(dismissStreak))
        }
    }

    fun logProReminderAction(action: String, dismissStreak: Int) {
        logEvent("pro_reminder_action") {
            putString("action", ProAnalyticsDimensions.reminderAction(action))
            putLong("dismiss_streak", ProAnalyticsDimensions.streak(dismissStreak))
        }
    }

    fun logProPurchaseResult(source: String, tier: String, result: String) {
        logEvent("pro_purchase_result") {
            putString("source", ProAnalyticsDimensions.purchaseSource(source))
            putString("tier", ProAnalyticsDimensions.tier(tier))
            putString("result", ProAnalyticsDimensions.purchaseResult(result))
        }
    }

    private inline fun logEvent(name: String, buildParams: Bundle.() -> Unit) {
        if (!ready.get()) return
        val analytics = firebaseAnalytics ?: return
        runCatching {
            analytics.logEvent(name, Bundle().apply(buildParams))
        }.onFailure { error ->
            Log.w(TAG, "Failed to log analytics event: $name", error)
        }
    }

    private fun Boolean.toAnalyticsLong(): Long = if (this) 1L else 0L
}

internal object AnalyticsDimensions {
    /** This app has no performance-profile choice; the fixed token keeps reports aligned across apps. */
    const val PROFILE_DEFAULT = "default"

    private val launchTypes = setOf("game", "bios", "autotest", "smoke_test")
    private val failureReasons = setOf("bios_missing", "path_unavailable", "native_start")
    private val saveStateActions = setOf("save", "load")

    fun sanitizeLaunchType(value: String): String = value.takeIf(launchTypes::contains) ?: "unknown"

    fun sanitizeFailureReason(value: String): String = value.takeIf(failureReasons::contains) ?: "unknown"

    fun sanitizeSaveStateAction(value: String): String = value.takeIf(saveStateActions::contains) ?: "unknown"

    fun renderer(value: String): String = when (value.lowercase()) {
        "vulkan" -> "vulkan"
        "opengl" -> "opengl"
        else -> "unknown"
    }

    fun audioBackend(value: String): String = when (value.lowercase()) {
        "sdl" -> "sdl"
        "cubeb" -> "cubeb"
        else -> "unknown"
    }

    fun upscale(value: Float): String = when {
        !value.isFinite() || value <= 1f -> "native"
        value < 2.5f -> "2x"
        value < 3.5f -> "3x"
        else -> "4x_plus"
    }

    fun durationBucket(activePlayTimeMs: Long): String = when {
        activePlayTimeMs < 60_000L -> "under_1m"
        activePlayTimeMs < 5 * 60_000L -> "1_5m"
        activePlayTimeMs < 15 * 60_000L -> "5_15m"
        activePlayTimeMs < 30 * 60_000L -> "15_30m"
        activePlayTimeMs < 60 * 60_000L -> "30_60m"
        else -> "over_60m"
    }
}

internal object ProAnalyticsDimensions {
    private val placements = setOf("library", "themes", "controls", "profile")
    private val promoActions = setOf("get_pro", "learn_more", "dismiss")
    private val reminderActions = setOf("get_pro", "later")
    private val tiers = setOf("base", "supporter", "patron")
    private val results = setOf("completed", "canceled", "failed", "pending")
    private val sources = setOf(
        "promo_library", "promo_themes", "promo_controls", "promo_profile",
        "reminder", "welcome", "onboarding", "settings", "other"
    )

    fun placement(value: String): String = value.takeIf(placements::contains) ?: "unknown"
    fun promoAction(value: String): String = value.takeIf(promoActions::contains) ?: "unknown"
    fun reminderAction(value: String): String = value.takeIf(reminderActions::contains) ?: "unknown"
    fun tier(value: String): String = value.takeIf(tiers::contains) ?: "unknown"
    fun purchaseResult(value: String): String = value.takeIf(results::contains) ?: "unknown"
    fun purchaseSource(value: String): String = value.takeIf(sources::contains) ?: "other"
    fun streak(value: Int): Long = value.coerceIn(0, 99).toLong()
}
