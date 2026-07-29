package com.sbro.emucorev.core

import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.isDirectory
import kotlin.io.path.readText
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PlayPolicyContractTest {
    @Test
    fun productionManifestDoesNotRequestRestrictedStorageOrInstallerPermissions() {
        val projectRoot = locateProjectRoot()
        val manifest = projectRoot.resolve("src/main/AndroidManifest.xml").readText()
        val forbidden = listOf(
            "MANAGE_EXTERNAL_STORAGE",
            "READ_EXTERNAL_STORAGE",
            "WRITE_EXTERNAL_STORAGE",
            "REQUEST_INSTALL_PACKAGES",
            "requestLegacyExternalStorage"
        )

        forbidden.forEach { token ->
            assertFalse("Production manifest must not contain $token", token in manifest)
        }
        assertTrue("Play Billing permission is required", "com.android.vending.BILLING" in manifest)
    }

    @Test
    fun appUpdateFlowDoesNotDownloadOrInstallApks() {
        val sourceRoot = locateProjectRoot().resolve("src/main/java")
        val repository = sourceRoot
            .resolve("com/sbro/emucorev/core/AppUpdateRepository.kt")
            .readText()
        val navigation = sourceRoot
            .resolve("com/sbro/emucorev/navigation/AppNavigation.kt")
            .readText()

        assertFalse("Update repository must not launch package installers", "launchInstaller" in repository)
        assertFalse("Update repository must not download APK files", "downloadApk(" in repository)
        assertFalse("Startup update dialogs must stay removed", "AppUpdateAvailableDialog" in navigation)
        assertFalse("Startup update checks must stay removed", "checkForStartupAppUpdates" in navigation)
    }

    @Test
    fun privacyPolicyCardUsesPublishedEmuCoreVUrl() {
        val settingsSource = locateProjectRoot()
            .resolve("src/main/java/com/sbro/emucorev/ui/settings/SettingsTabContent.kt")
            .readText()
        assertTrue(
            "Privacy policy card must use the published Google Sites URL",
            "https://sites.google.com/view/privacy-policy-for-emucorev/" in settingsSource
        )
        assertTrue("Privacy policy card must be visible in About", "settings_about_privacy_policy" in settingsSource)
    }

    private fun locateProjectRoot(): Path {
        val workingDirectory = Path.of(System.getProperty("user.dir"))
        return sequenceOf(
            workingDirectory,
            workingDirectory.resolve("app")
        ).firstOrNull { Files.isDirectory(it.resolve("src/main")) }
            ?: error("Unable to locate Android app module from $workingDirectory")
    }
}
