package com.sbro.emucorev.core

import android.content.Context
import android.media.AudioAttributes
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.view.HapticFeedbackConstants
import android.view.View
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import kotlin.math.pow
import kotlin.math.roundToInt

object AndroidTouchHaptics {
    enum class ButtonPhase {
        PRESS,
        RELEASE
    }

    private val hapticAudioAttributes: AudioAttributes by lazy {
        AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_GAME)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build()
    }

    /**
     * Resolving the vibrator and querying its capabilities are binder calls into
     * the system vibrator service. Doing that per button press adds several
     * milliseconds of jitter to the input path, so both are resolved once.
     */
    @Volatile
    private var cachedVibrator: Vibrator? = null

    @Volatile
    private var vibratorResolved: Boolean = false

    @Volatile
    private var cachedAmplitudeControl: Boolean = false

    /**
     * Vibration is dispatched off the caller's thread so a slow or contended
     * vibrator service can never delay the button reaching the emulator core.
     */
    private val hapticExecutor: ExecutorService by lazy {
        Executors.newSingleThreadExecutor { runnable ->
            Thread(runnable, "touch-haptics").apply {
                isDaemon = true
                priority = Thread.MAX_PRIORITY
            }
        }
    }

    private fun resolveVibrator(context: Context): Vibrator? {
        if (!vibratorResolved) {
            synchronized(this) {
                if (!vibratorResolved) {
                    val vibrator = findVibrator(context)
                    cachedVibrator = vibrator
                    cachedAmplitudeControl = vibrator != null &&
                        runCatching { vibrator.hasAmplitudeControl() }.getOrDefault(false)
                    vibratorResolved = true
                }
            }
        }
        return cachedVibrator
    }

    fun playButton(
        context: Context,
        view: View? = null,
        strengthPercent: Int,
        preset: Int,
        phase: ButtonPhase
    ) {
        val vibrator = resolveVibrator(context)
        if (vibrator == null) {
            performViewHaptic(view, phase)
            return
        }

        val hasAmplitudeControl = cachedAmplitudeControl
        runCatching {
            hapticExecutor.execute {
                runCatching {
                    vibrate(
                        vibrator,
                        createButtonEffect(hasAmplitudeControl, strengthPercent, preset, phase)
                    )
                }
            }
        }
    }

    @Suppress("DEPRECATION")
    private fun performViewHaptic(view: View?, phase: ButtonPhase) {
        view ?: return
        val flags = HapticFeedbackConstants.FLAG_IGNORE_VIEW_SETTING or
            HapticFeedbackConstants.FLAG_IGNORE_GLOBAL_SETTING
        val feedback = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (phase == ButtonPhase.PRESS) {
                HapticFeedbackConstants.GESTURE_START
            } else {
                HapticFeedbackConstants.GESTURE_END
            }
        } else {
            if (phase == ButtonPhase.PRESS) {
                HapticFeedbackConstants.VIRTUAL_KEY
            } else {
                HapticFeedbackConstants.CLOCK_TICK
            }
        }
        runCatching { view.performHapticFeedback(feedback, flags) }
    }

    private fun createButtonEffect(
        hasAmplitudeControl: Boolean,
        strengthPercent: Int,
        preset: Int,
        phase: ButtonPhase
    ): VibrationEffect {
        data class Pulse(
            val durationMs: Long,
            val minimumAmplitude: Int,
            val defaultAmplitude: Int,
            val maximumAmplitude: Int
        )

        val profile = when (preset.coerceIn(0, 3)) {
            VitaCoreConfig.TOUCH_HAPTICS_PRESET_SOFT ->
                if (phase == ButtonPhase.PRESS) Pulse(18L, 20, 89, 148)
                else Pulse(9L, 10, 46, 77)
            VitaCoreConfig.TOUCH_HAPTICS_PRESET_CRISP ->
                if (phase == ButtonPhase.PRESS) Pulse(13L, 38, 165, 255)
                else Pulse(7L, 18, 104, 173)
            VitaCoreConfig.TOUCH_HAPTICS_PRESET_STRONG ->
                if (phase == ButtonPhase.PRESS) Pulse(32L, 55, 191, 255)
                else Pulse(20L, 28, 125, 209)
            else ->
                if (phase == ButtonPhase.PRESS) Pulse(24L, 28, 153, 255)
                else Pulse(14L, 14, 98, 163)
        }
        if (!hasAmplitudeControl) {
            if (
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q &&
                strengthPercent.coerceIn(10, 100) == 60
            ) {
                return VibrationEffect.createPredefined(
                    when {
                        preset == VitaCoreConfig.TOUCH_HAPTICS_PRESET_SOFT ->
                            VibrationEffect.EFFECT_TICK
                        preset == VitaCoreConfig.TOUCH_HAPTICS_PRESET_STRONG &&
                            phase == ButtonPhase.PRESS ->
                            VibrationEffect.EFFECT_HEAVY_CLICK
                        phase == ButtonPhase.PRESS -> VibrationEffect.EFFECT_CLICK
                        else -> VibrationEffect.EFFECT_TICK
                    }
                )
            }
            val durationFactor = if (strengthPercent <= 60) {
                0.55f +
                    ((strengthPercent.coerceIn(10, 60) - 10) / 50f) * 0.45f
            } else {
                1f +
                    ((strengthPercent.coerceIn(60, 100) - 60) / 40f) * 0.25f
            }
            val duration = (profile.durationMs * durationFactor)
                .roundToInt()
                .coerceAtLeast(6)
                .toLong()
            return VibrationEffect.createOneShot(
                duration,
                VibrationEffect.DEFAULT_AMPLITUDE
            )
        }

        val strength = strengthPercent.coerceIn(10, 100)
        val amplitude = if (strength <= 60) {
            val progress = ((strength - 10) / 50f).pow(0.82f)
            profile.minimumAmplitude +
                (profile.defaultAmplitude - profile.minimumAmplitude) * progress
        } else {
            val progress = ((strength - 60) / 40f).pow(0.82f)
            profile.defaultAmplitude +
                (profile.maximumAmplitude - profile.defaultAmplitude) * progress
        }.roundToInt().coerceIn(
            profile.minimumAmplitude,
            profile.maximumAmplitude
        )
        return VibrationEffect.createOneShot(profile.durationMs, amplitude)
    }

    @Suppress("DEPRECATION")
    private fun vibrate(vibrator: Vibrator, effect: VibrationEffect) {
        vibrator.vibrate(effect, hapticAudioAttributes)
    }

    private fun findVibrator(context: Context): Vibrator? {
        val appContext = context.applicationContext
        val candidates = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val manager = appContext.getSystemService(VibratorManager::class.java)
            buildList {
                manager?.defaultVibrator?.let(::add)
                manager?.vibratorIds?.forEach { id ->
                    runCatching { manager.getVibrator(id) }
                        .getOrNull()
                        ?.let(::add)
                }
            }
        } else {
            @Suppress("DEPRECATION")
            listOfNotNull(
                appContext.getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
            )
        }
        return candidates
            .distinctBy { System.identityHashCode(it) }
            .firstOrNull {
                runCatching { it.hasVibrator() }.getOrDefault(false)
            }
    }
}
