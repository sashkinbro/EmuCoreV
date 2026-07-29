package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Test

class AppUpdateReleaseParserTest {
    @Test
    fun `keeps regular and parallel APK assets separate`() {
        val assets = classifyAppUpdateAssets(
            listOf(
                AppUpdateAsset("EmuCoreV-v0.1.6.apk", "https://example.com/main.apk", 1000),
                AppUpdateAsset("EmuCoreV-v0.1.6-parallel.apk", "https://example.com/parallel.apk", 2000)
            )
        )

        assertEquals("https://example.com/main.apk", assets.regular?.downloadUrl)
        assertEquals("https://example.com/parallel.apk", assets.parallel?.downloadUrl)
        assertEquals(2000L, assets.parallel?.sizeBytes)
    }

    @Test
    fun `does not offer parallel download when release has only normal APK`() {
        val assets = classifyAppUpdateAssets(
            listOf(AppUpdateAsset("EmuCoreV-v0.1.5.apk", "https://example.com/main.apk", 1000))
        )

        assertEquals("https://example.com/main.apk", assets.regular?.downloadUrl)
        assertEquals(null, assets.parallel)
    }

    @Test
    fun `ignores non APK and unavailable parallel assets`() {
        val assets = classifyAppUpdateAssets(
            listOf(
                AppUpdateAsset("checksums.txt", "https://example.com/checksums.txt", 10),
                AppUpdateAsset("EmuCoreV-parallel.apk", null, 2000)
            )
        )

        assertEquals(null, assets.regular)
        assertEquals(null, assets.parallel)
    }
}
