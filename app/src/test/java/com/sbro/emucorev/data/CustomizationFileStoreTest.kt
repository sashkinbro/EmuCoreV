package com.sbro.emucorev.data

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CustomizationFileStoreTest {
    @Test
    fun backgroundValidationAcceptsSupportedImagesAnimationsAndVideos() {
        assertTrue(CustomizationFileStore.isSupportedBackground("image/png", "png"))
        assertTrue(CustomizationFileStore.isSupportedBackground("image/gif", "gif"))
        assertTrue(CustomizationFileStore.isSupportedBackground("video/mp4", "mp4"))
        assertTrue(CustomizationFileStore.isSupportedBackground(null, "webm"))
    }

    @Test
    fun backgroundValidationRejectsUnrelatedFiles() {
        assertFalse(CustomizationFileStore.isSupportedBackground("application/zip", "zip"))
        assertFalse(CustomizationFileStore.isSupportedBackground(null, "exe"))
    }
}
