package com.sbro.emucorev.nativeui

import kotlin.math.max

/**
 * Decides which UI sound requests become voices and how loud they are, so rapid navigation
 * never stacks the same short effect into clipping, never exceeds the mixer's voice budget
 * (a stolen stream cuts mid-waveform and clicks) and never allocates. Pure logic: the caller
 * supplies a monotonic clock and plays the accepted requests.
 *
 * Not thread safe; use from a single (sound) thread.
 */
class SoundVoicePolicy(
    /** Effect length in milliseconds by key; keys not listed are rejected. */
    private val durationsMs: Map<Int, Int>,
    private val maxVoices: Int = DEFAULT_MAX_VOICES,
    private val baseVolume: Float = DEFAULT_VOLUME,
) {
    private val endsAt = LongArray(maxVoices * 2)
    private var voices = 0
    private val lastAccepted = HashMap<Int, Long>(durationsMs.size * 2)

    /** Volume of the accepted voice, valid right after [accept] returned true. */
    var volume: Float = baseVolume
        private set

    /** True when [key] should start now; then [volume] holds its gain. */
    fun accept(key: Int, nowMs: Long): Boolean {
        val length = durationsMs[key] ?: return false
        // Voices that finished their effect free their slot.
        var kept = 0
        for (i in 0 until voices) {
            if (endsAt[i] > nowMs) {
                endsAt[kept++] = endsAt[i]
            }
        }
        voices = kept
        // A retrigger inside a fraction of the effect only adds a doubled attack (comb/crackle);
        // dropping it is inaudible as a missing tick next to the one that just started.
        val previous = lastAccepted[key]
        if (previous != null && nowMs - previous < max(MIN_RETRIGGER_MS, length / 3).toLong().coerceAtMost(RETRIGGER_CAP_MS)) {
            return false
        }
        val priority = priority(key)
        if (voices >= maxVoices) {
            // Out of voices: only the effects the user is waiting for may exceed the budget,
            // and only by a little; navigation ticks are dropped instead of cutting another voice.
            if (priority < HIGH_PRIORITY || voices >= maxVoices * 2) return false
        }
        volume = baseVolume / (1f + OVERLAP_ATTENUATION * voices)
        endsAt[voices++] = nowMs + length
        lastAccepted[key] = nowMs
        return true
    }

    /** Number of effects still sounding at [nowMs] (for tests and diagnostics). */
    fun activeVoices(nowMs: Long): Int = (0 until voices).count { endsAt[it] > nowMs }

    private fun priority(key: Int): Int = when (key) {
        NativeUiSoundBank.SOUND_DECIDE, NativeUiSoundBank.SOUND_CANCEL -> HIGH_PRIORITY
        else -> 0
    }

    companion object {
        const val DEFAULT_MAX_VOICES = 4
        const val DEFAULT_VOLUME = 0.72f
        private const val MIN_RETRIGGER_MS = 18
        private const val RETRIGGER_CAP_MS = 45L
        private const val OVERLAP_ATTENUATION = 0.5f
        private const val HIGH_PRIORITY = 1
    }
}
