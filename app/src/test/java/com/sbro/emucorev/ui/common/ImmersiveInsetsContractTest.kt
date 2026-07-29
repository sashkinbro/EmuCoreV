package com.sbro.emucorev.ui.common

import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.isDirectory
import kotlin.io.path.readText
import org.junit.Assert.assertTrue
import org.junit.Test

class ImmersiveInsetsContractTest {
    @Test
    fun fullscreenScreensKeepPersistentCameraSafeInsets() {
        val sourceRoot = locateSourceRoot()
        val transientStatusInset = Regex(
            """WindowInsets\.statusBars(?!IgnoringVisibility)|\.statusBarsPadding\("""
        )

        val offenders = Files.walk(sourceRoot).use { paths ->
            paths
                .filter { it.toString().endsWith(".kt") }
                .filter { transientStatusInset.containsMatchIn(it.readText()) }
                .map { sourceRoot.relativize(it).toString() }
                .sorted()
                .toList()
        }

        assertTrue(
            "Fullscreen screens must reserve statusBarsIgnoringVisibility for camera safety: $offenders",
            offenders.isEmpty()
        )

        val emulationMenu = sourceRoot
            .resolve("com/sbro/emucorev/ui/emulation/EmulationMenu.kt")
            .readText()
        assertTrue(
            "The in-game menu must keep its 4 dp spacing outside the panel",
            ".padding(vertical = 4.dp)" in emulationMenu
        )
        assertTrue(
            "The quick-bar island must stay at its original 12 dp top position",
            "modifier = modifier.padding(top = 12.dp)" in emulationMenu
        )

        listOf(
            "home/HomeScreen.kt",
            "library/LibraryScreen.kt",
            "catalog/CatalogScreen.kt",
            "detail/GameDetailScreen.kt",
            "setup/SetupScreen.kt",
            "profile/ProfileScreen.kt",
            "saves/SaveDataScreen.kt",
            "playtime/PlayTimeScreen.kt",
            "achievements/AchievementsScreen.kt",
            "gamemanager/GameManagerScreen.kt",
            "settings/AppLanguageScreen.kt",
            "settings/VitaLanguageScreen.kt",
            "settings/GpuDriverScreen.kt",
            "settings/SettingsScreen.kt"
        ).forEach { relativePath ->
            val source = sourceRoot
                .resolve("com/sbro/emucorev/ui/$relativePath")
                .readText()
            assertTrue(
                "$relativePath must derive its top inset directly from the persistent safe area",
                Regex(
                    """val topInset = WindowInsets\.statusBarsIgnoringVisibility\.asPaddingValues\(\)\.calculateTopPadding\(\)"""
                ).containsMatchIn(source)
            )
        }
    }

    private fun locateSourceRoot(): Path {
        val workingDirectory = Path.of(System.getProperty("user.dir"))
        return sequenceOf(
            workingDirectory.resolve("src/main/java"),
            workingDirectory.resolve("app/src/main/java")
        ).firstOrNull(Path::isDirectory)
            ?: error("Unable to locate app/src/main/java from $workingDirectory")
    }
}
