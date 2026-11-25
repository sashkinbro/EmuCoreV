package com.sbro.emucorev.nativeui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** B049: rapid input must not stack shell sounds into clipping or exhaust the voice budget. */
class SoundVoicePolicyTest {
    private val decide = NativeUiSoundBank.SOUND_DECIDE
    private val cancel = NativeUiSoundBank.SOUND_CANCEL
    private val focus = NativeUiSoundBank.SOUND_FOCUS
    private val scroll = NativeUiSoundBank.SOUND_PAGE_SCROLL
    private val turn = NativeUiSoundBank.SOUND_PAGE_TURN

    // Lengths of the real effects: 111, 71, 46, 99 and 363 ms.
    private fun policy(maxVoices: Int = 4) = SoundVoicePolicy(
        mapOf(decide to 111, cancel to 71, focus to 46, scroll to 99, turn to 363), maxVoices,
    )

    @Test
    fun firstVoicePlaysAtBaseVolume() {
        val policy = policy()
        assertTrue(policy.accept(focus, 0))
        assertEquals(SoundVoicePolicy.DEFAULT_VOLUME, policy.volume, 0f)
    }

    @Test
    fun unknownEffectIsRejected() {
        assertFalse(policy().accept(99, 0))
    }

    @Test
    fun doubleTriggerInsideTheAttackIsDropped() {
        val policy = policy()
        assertTrue(policy.accept(focus, 100))
        assertFalse(policy.accept(focus, 105))
        assertTrue(policy.accept(focus, 130))
    }

    @Test
    fun overlappingVoicesAreAttenuatedSoTheSumDoesNotClip() {
        val policy = policy()
        assertTrue(policy.accept(turn, 0))
        val first = policy.volume
        assertTrue(policy.accept(focus, 50))
        val second = policy.volume
        assertTrue(second < first)
        assertTrue(policy.accept(scroll, 60))
        assertTrue(policy.volume < second)
        // Whatever mix is sounding, the summed gain stays near full scale.
        assertTrue(first + second + policy.volume < 1.6f)
    }

    @Test
    fun voiceBudgetDropsNavigationTicksButNotConfirmations() {
        val policy = policy(maxVoices = 2)
        assertTrue(policy.accept(turn, 0))
        assertTrue(policy.accept(scroll, 30))
        assertFalse("a focus tick must not cut a running voice", policy.accept(focus, 60))
        assertTrue("the confirmation still sounds", policy.accept(decide, 70))
    }

    @Test
    fun finishedVoicesFreeTheirSlot() {
        val policy = policy(maxVoices = 1)
        assertTrue(policy.accept(turn, 0))
        assertFalse(policy.accept(focus, 100))
        assertTrue(policy.accept(focus, 400))
        assertEquals(1, policy.activeVoices(400))
    }

    @Test
    fun rapidFocusStormStaysBounded() {
        val policy = policy()
        var accepted = 0
        var maxActive = 0
        // 100 focus moves 4 ms apart (held d-pad plus a touch drag), 400 ms in total.
        for (i in 0 until 100) {
            val now = i * 4L
            if (policy.accept(focus, now)) accepted++
            maxActive = maxOf(maxActive, policy.activeVoices(now))
        }
        assertTrue("accepted $accepted", accepted in 10..25)
        assertTrue("max active $maxActive", maxActive <= 4)
    }
}
