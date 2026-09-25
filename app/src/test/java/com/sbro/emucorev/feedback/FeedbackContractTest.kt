package com.sbro.emucorev.feedback

import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.readText
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class FeedbackContractTest {
    @Test
    fun attachmentAndMessageLimitsMatchTheSharedWorkerContract() {
        assertEquals(5, FeedbackLimits.MAX_ATTACHMENTS)
        assertEquals(49L * 1024L * 1024L, FeedbackLimits.MAX_ATTACHMENT_BYTES)
        assertEquals(90L * 1024L * 1024L, FeedbackLimits.MAX_TOTAL_BYTES)
        assertEquals(3_000, FeedbackLimits.MAX_MESSAGE_LENGTH)
    }

    @Test
    fun uploadRequiresHttpsAndUsesTheSharedAuthenticatedHeader() {
        val root = sourceRoot()
        val scheduler = root.resolve("feedback/FeedbackUploadScheduler.kt").readText()
        val worker = root.resolve("feedback/FeedbackUploadWorker.kt").readText()

        assertTrue("Feedback configuration must require HTTPS", "startsWith(\"https://\")" in scheduler)
        assertTrue("Uploads must wait for a connected network", "NetworkType.CONNECTED" in scheduler)
        assertTrue("Shared Worker auth header is missing", "\"X-EmuCoreX-Key\"" in worker)
        assertTrue("The API key must come from BuildConfig", "BuildConfig.FEEDBACK_API_KEY" in worker)
        assertFalse("No feedback secret may be hard-coded in source", "CJRYXCTVBR5VO" in worker)
    }

    @Test
    fun feedbackNavigationAndDiscordScreenAreWired() {
        val root = sourceRoot()
        val shell = root.resolve("navigation/AdaptiveShell.kt").readText()
        val navigation = root.resolve("navigation/AppNavigation.kt").readText()
        val settings = root.resolve("ui/settings/SettingsTabContent.kt").readText()

        assertTrue("Feedback drawer destination is missing", "PrimaryDestination.Feedback" in shell)
        assertTrue("Feedback drawer item is missing", "R.string.feedback_title" in shell)
        assertFalse("The legacy Discord invite drawer action must stay removed", "shell_discord_server" in shell)
        assertTrue("Feedback route is missing", "composable(ROUTE_FEEDBACK)" in navigation)
        assertTrue("Feedback screen is not wired", "FeedbackScreen(" in navigation)
        assertTrue("Discord destination is missing", "Discord, Feedback" in shell)
        assertTrue("Discord selection is missing", "PrimaryDestination.Discord" in navigation)
        assertTrue("Discord route is missing", "composable(ROUTE_DISCORD)" in navigation)
        assertTrue("Discord screen is not wired", "DiscordScreen(" in navigation)
        assertTrue("Settings must open the Discord screen", "onOpenDiscord = navigateDiscord" in navigation)
        assertTrue("About tab must navigate to Discord", "onClick = onOpenDiscord" in settings)
    }

    private fun sourceRoot(): Path {
        val workingDirectory = Path.of(System.getProperty("user.dir"))
        val appModule = sequenceOf(workingDirectory, workingDirectory.resolve("app"))
            .firstOrNull { Files.isDirectory(it.resolve("src/main")) }
            ?: error("Unable to locate Android app module from $workingDirectory")
        return appModule.resolve("src/main/java/com/sbro/emucorev")
    }
}
