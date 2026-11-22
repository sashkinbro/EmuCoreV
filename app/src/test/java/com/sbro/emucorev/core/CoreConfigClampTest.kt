package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Test

class CoreConfigClampTest {
    @Test fun outOfRangeValuesAreClampedToTheSliderRanges() {
        val clamped = VitaCoreConfig(
            audioVolume = 500, bgmVolume = -3, overlayScale = 9f, overlayOpacity = 0, resolutionMultiplier = 64f
        ).clampedToUiRanges()
        assertEquals(100, clamped.audioVolume)
        assertEquals(0, clamped.bgmVolume)
        assertEquals(2f, clamped.overlayScale, 0f)
        assertEquals(10, clamped.overlayOpacity)
        assertEquals(8f, clamped.resolutionMultiplier, 0f)
    }

    @Test fun lowerBoundsAndNaNAreHandled() {
        val clamped = VitaCoreConfig(
            overlayScale = 0.01f, resolutionMultiplier = Float.NaN, audioVolume = Int.MIN_VALUE
        ).clampedToUiRanges()
        assertEquals(0.5f, clamped.overlayScale, 0f)
        assertEquals(1f, clamped.resolutionMultiplier, 0f)
        assertEquals(0, clamped.audioVolume)
    }

    @Test fun inRangeValuesAreUntouched() {
        val config = VitaCoreConfig(audioVolume = 40, bgmVolume = 70, overlayScale = 1.3f, overlayOpacity = 55, resolutionMultiplier = 2.5f)
        assertEquals(config, config.clampedToUiRanges())
    }
}
