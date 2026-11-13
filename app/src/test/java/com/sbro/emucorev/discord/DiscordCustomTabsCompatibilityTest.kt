package com.sbro.emucorev.discord

import androidx.browser.customtabs.CustomTabsIntent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [29])
@GraphicsMode(GraphicsMode.Mode.LEGACY)
class DiscordCustomTabsCompatibilityTest {
    @Test
    fun `discord custom tabs options are available on Android 10`() {
        val intent = CustomTabsIntent.Builder()
            .setBookmarksButtonEnabled(false)
            .setDownloadButtonEnabled(false)
            .setShareState(2)
            .setSendToExternalDefaultHandlerEnabled(true)
            .setShowTitle(false)
            .setInitialActivityHeightPx(900, CustomTabsIntent.ACTIVITY_HEIGHT_FIXED)
            .build()
            .intent

        assertTrue(intent.getBooleanExtra(CustomTabsIntent.EXTRA_DISABLE_BOOKMARKS_BUTTON, false))
        assertTrue(intent.getBooleanExtra(CustomTabsIntent.EXTRA_DISABLE_DOWNLOAD_BUTTON, false))
        assertEquals(2, intent.getIntExtra(CustomTabsIntent.EXTRA_SHARE_STATE, -1))
        assertTrue(intent.getBooleanExtra(CustomTabsIntent.EXTRA_SEND_TO_EXTERNAL_DEFAULT_HANDLER, false))
        assertEquals(900, intent.getIntExtra(CustomTabsIntent.EXTRA_INITIAL_ACTIVITY_HEIGHT_PX, -1))
        assertEquals(
            CustomTabsIntent.ACTIVITY_HEIGHT_FIXED,
            CustomTabsIntent.getActivityResizeBehavior(intent)
        )
    }
}
