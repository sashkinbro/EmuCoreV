package com.sbro.emucorev.ui.common

import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.isDirectory
import kotlin.io.path.readText
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ScreenTopBarContractTest {
    @Test
    fun sharedTopBarMatchesTheEmuCoreXContainerContract() {
        val source = sourceRoot().resolve("ui/common/ScreenTopBar.kt").readText()

        assertTrue("Top bar must use the theme-aware 24 dp shape", "neonShape(24.dp)" in source)
        assertTrue("Top bar must remain translucent", "surface.copy(alpha = 0.78f)" in source)
        assertTrue("Top bar must use subtle tonal elevation", "tonalElevation = 1.dp" in source)
        assertTrue("Top bar must not cast a floating shadow", "shadowElevation = 0.dp" in source)
        assertTrue("Top bar border opacity must match EmuCoreX", "copy(alpha = 0.62f)" in source)
        assertTrue("Neon top bar must use the tricolor divider", "NeonTricolorDivider" in source)
        assertTrue("Neon top bar must use the yellow accent border", "NeonYellow.copy(alpha = 0.35f)" in source)
        assertTrue(
            "Top bar must keep the compact internal padding",
            "start = 10.dp + if (LocalNeonTheme.current && !neonDecorated) 8.dp else 0.dp" in source &&
                "top = 8.dp" in source &&
                "bottom = 8.dp" in source
        )
        assertTrue(
            "Only explicitly selected navigation headers may render the Neon divider",
            "neonDecorated: Boolean = false" in source &&
                "showNeonDivider: Boolean = false" in source &&
                "if (LocalNeonTheme.current && neonDecorated && showNeonDivider)" in source &&
                "showNeonDivider = showNeonDivider" in source
        )
    }

    @Test
    fun neonDividerIsLimitedToAchievementsAndGpuDriverManager() {
        val root = sourceRoot()
        val allowed = setOf(
            "ui/achievements/AchievementsScreen.kt",
            "ui/settings/GpuDriverScreen.kt"
        )
        val users = Files.walk(root).use { paths ->
            paths
                .filter { it.toString().endsWith(".kt") }
                .filter { "showNeonDivider = true" in it.readText() }
                .map { root.relativize(it).toString().replace('\\', '/') }
                .toList()
                .toSet()
        }

        assertTrue("Unexpected Neon divider users: $users", users == allowed)
    }

    @Test
    fun primaryScreensUseAContainedTopBar() {
        val sourceRoot = sourceRoot()
        val requiredScreens = listOf(
            "ui/home/HomeScreen.kt",
            "ui/library/LibraryScreen.kt",
            "ui/catalog/CatalogScreen.kt",
            "ui/setup/SetupScreen.kt",
            "ui/settings/AppLanguageScreen.kt",
            "ui/settings/VitaLanguageScreen.kt",
            "ui/settings/GpuDriverScreen.kt",
            "ui/saves/SaveDataScreen.kt",
            "ui/playtime/PlayTimeScreen.kt",
            "ui/achievements/AchievementsScreen.kt",
            "ui/profile/ProfileScreen.kt",
            "ui/gamemanager/GameManagerScreen.kt"
        )

        val offenders = requiredScreens.filter { relativePath ->
            val source = sourceRoot.resolve(relativePath).readText()
            "ScreenTopBar(" !in source && "ScreenTopBarSurface" !in source
        }

        assertTrue("Primary screens missing the shared top bar: $offenders", offenders.isEmpty())
    }

    @Test
    fun navigationButtonsMatchTheContainedHeaderStyle() {
        val sourceRoot = sourceRoot()
        listOf("NavigationBackButton.kt", "NavigationMenuButton.kt").forEach { fileName ->
            val source = sourceRoot.resolve("ui/common/$fileName").readText()
            assertTrue("$fileName must be 44 dp", ".size(44.dp)" in source)
            assertTrue("$fileName must use the theme-aware 14 dp shape", "neonShape(14.dp)" in source)
            assertTrue("$fileName must not float", "shadowElevation: Dp = 0.dp" in source)
            assertTrue("$fileName must use the subtle border", "copy(alpha = 0.50f)" in source)
        }
        val backButton = sourceRoot.resolve("ui/common/NavigationBackButton.kt").readText()
        val menuButton = sourceRoot.resolve("ui/common/NavigationMenuButton.kt").readText()
        assertTrue("Back navigation keeps the Neon red accent", "LocalNeonTheme.current" in backButton)
        assertFalse("Root menu button must stay neutral like EmuCoreX", "LocalNeonTheme.current" in menuButton)
    }

    @Test
    fun libraryHeaderMatchesTheNeutralEmuCoreXHomeHeader() {
        val source = sourceRoot().resolve("ui/library/LibraryScreen.kt").readText()

        assertTrue("Library title must use the X headline scale", "MaterialTheme.typography.headlineMedium" in source)
        assertTrue("Library actions must share the X grouped surface", "shape = neonShape(18.dp)" in source)
        assertTrue("Library action group must use the X alpha", "surfaceVariant.copy(alpha = 0.38f)" in source)
        assertTrue("Library action group must contain exactly two separators", source.countOccurrences("LibraryHeaderActionDivider()") == 3)
    }

    @Test
    fun animatedLibrarySearchDoesNotCreateDoubleSpacingBelowTopBar() {
        val source = sourceRoot().resolve("ui/library/LibraryScreen.kt").readText()
        val listContent = source
            .substringAfter("LazyColumn(")
            .substringBefore("if (uiState.isLoading)")

        assertTrue(
            "Top bar and animated search must share one lazy-list item",
            Regex("""item \{\s*Column \{\s*ScreenTopBarSurface""").containsMatchIn(listContent)
        )
        assertTrue(
            "The top-bar-to-search gap must collapse as part of the search animation",
            Regex(
                """AnimatedVisibility\([\s\S]*?visible = searchExpanded[\s\S]*?Column \{\s*Spacer\(modifier = Modifier\.height\(14\.dp\)\)"""
            ).containsMatchIn(listContent)
        )
        assertFalse(
            "Search must not create a separate conditional lazy item",
            Regex("""if \(searchExpanded\)\s*\{\s*item\s*\{""").containsMatchIn(listContent)
        )
    }

    private fun sourceRoot(): Path {
        val workingDirectory = Path.of(System.getProperty("user.dir"))
        val appModule = sequenceOf(
            workingDirectory,
            workingDirectory.resolve("app")
        ).firstOrNull { Files.isDirectory(it.resolve("src/main")) }
            ?: error("Unable to locate Android app module from $workingDirectory")
        return appModule.resolve("src/main/java/com/sbro/emucorev")
    }

    private fun String.countOccurrences(token: String): Int = split(token).size - 1
}
