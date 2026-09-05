package com.sbro.emucorev.ui.theme

import java.nio.file.Files
import java.nio.file.Path
import javax.xml.parsers.DocumentBuilderFactory
import kotlin.io.path.isDirectory
import kotlin.io.path.readText
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class NeonThemeContractTest {
    @Test
    fun neonThemeKeepsTheEmuCoreXPaletteTypographyAndShapeContract() {
        val root = sourceRoot()
        val neon = root.resolve("ui/theme/neon/NeonTheme.kt").readText()
        val theme = root.resolve("ui/theme/Theme.kt").readText()

        listOf(
            "NeonYellow = Color(0xFFFCEE0A)",
            "NeonRed = Color(0xFFFF003C)",
            "NeonBlue = Color(0xFF00F0FF)",
            "NeonBlack = Color(0xFF050505)",
            "NeonDark = Color(0xFF0F0F0F)",
            "NeonGray = Color(0xFF202020)",
            "val NeonColorScheme",
            "val NeonShapes",
            "fun Typography.neonMonospace()",
            "fun NeonCrtOverlay("
        ).forEach { contract ->
            assertTrue("Missing Neon contract: $contract", contract in neon)
        }
        assertTrue("Theme enum must expose Neon", "SYSTEM, LIGHT, DARK, PRO, NEON" in theme)
        assertTrue("Neon must select its exact color scheme", "ThemeMode.NEON -> NeonColorScheme" in theme)
        assertTrue("Neon must select cut-corner Material shapes", "ThemeMode.NEON) NeonShapes" in theme)
        assertTrue("Neon must select monospace typography", "baseTypography.neonMonospace()" in theme)
        assertTrue("Neon activation must be available to every composable", "LocalNeonTheme provides" in theme)
        assertTrue("Non-Neon shape fallback must remain rounded", "RoundedCornerShape(size)" in neon)
        assertFalse("Neon shape helper must never recurse", "return neonShape(size)" in neon)
    }

    @Test
    fun neonSelectionIsPersistentFreeAndOrderedAfterStandardThemes() {
        val root = sourceRoot()
        val preferences = root.resolve("data/AppPreferences.kt").readText()
        val settings = root.resolve("ui/settings/CustomizationTab.kt").readText()

        assertTrue("Preference value 4 must restore Neon", "4 -> ThemeMode.NEON" in preferences)
        assertTrue("Neon must persist as preference value 4", "ThemeMode.NEON -> 4" in preferences)
        assertFalse(
            "Neon must not be gated behind Pro",
            Regex("ThemeMode\\.NEON[\\s\\S]{0,80}!proUnlocked").containsMatchIn(preferences)
        )
        val system = settings.indexOf("ThemeMode.SYSTEM")
        val light = settings.indexOf("ThemeMode.LIGHT")
        val dark = settings.indexOf("ThemeMode.DARK")
        val neon = settings.indexOf("ThemeMode.NEON")
        val pro = settings.indexOf("ThemeMode.PRO")
        assertTrue("Theme choices must be System, Light, Dark, Neon, Pro", system < light && light < dark && dark < neon && neon < pro)
        assertTrue(
            "Selecting Neon must switch touch controls to Modern only on the selection edge",
            "if (themeMode != ThemeMode.NEON)" in settings &&
                "updateTouchControlVisualStyle(TouchControlVisualStyle.MODERN)" in settings
        )
        assertFalse(
            "Rendering Neon must never keep forcing the user's touch-control choice",
            "LaunchedEffect(themeMode)" in settings
        )
    }

    @Test
    fun everyLocaleContainsTheNeonThemeLabel() {
        val resourceRoot = resourceRoot()
        val stringFiles = Files.list(resourceRoot).use { paths ->
            paths
                .filter { it.fileName.toString() == "values" || it.fileName.toString().startsWith("values-") }
                .map { it.resolve("strings.xml") }
                .filter(Files::isRegularFile)
                .toList()
        }
        assertEquals(12, stringFiles.size)
        stringFiles.forEach { file ->
            val document = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(file.toFile())
            val nodes = document.getElementsByTagName("string")
            val names = (0 until nodes.length).mapNotNull { index ->
                nodes.item(index).attributes?.getNamedItem("name")?.nodeValue
            }
            assertTrue("Missing settings_theme_neon in ${file.parent.fileName}", "settings_theme_neon" in names)
        }
    }

    @Test
    fun crtOverlayNeverCoversTheRenderedGame() {
        val root = sourceRoot()
        val emulator = root.resolve("core/vita/Emulator.kt").readText()
        val menu = root.resolve("ui/emulation/EmulationMenu.kt").readText()

        assertTrue(
            "The full-screen emulator theme must disable its global CRT layer",
            "EmuCoreVTheme(themeMode = themeMode, enableCrtOverlay = false)" in emulator
        )
        assertTrue(
            "CRT scanlines must be composed only inside game-menu surfaces",
            menu.countOccurrences("NeonCrtOverlay()") >= 3 &&
                "if (LocalNeonTheme.current)" in menu
        )
    }

    @Test
    fun hardCodedUiCardsUseThemeAwareShapes() {
        val root = sourceRoot()
        val uiRoots = listOf(root.resolve("ui"), root.resolve("navigation"))
        val allowedGameOverlayFiles = setOf(
            root.resolve("ui/emulation/EmulationOverlay.kt"),
            root.resolve("ui/common/VectorTouchControls.kt")
        )
        val neonImplementation = root.resolve("ui/theme/neon")
        val offenders = mutableListOf<String>()
        uiRoots.forEach { uiRoot ->
            Files.walk(uiRoot).use { paths ->
                paths
                    .filter { file -> file.toString().endsWith(".kt") && !file.startsWith(neonImplementation) }
                    .forEach { file ->
                        file.readText().lineSequence()
                            .filter { line -> Regex("""RoundedCornerShape\([^)]*\.dp""").containsMatchIn(line) }
                            .filterNot { file in allowedGameOverlayFiles }
                            .mapTo(offenders) { line -> "${root.relativize(file)}: ${line.trim()}" }
                    }
            }
        }
        assertTrue("Hard-coded UI cards bypass Neon shapes: $offenders", offenders.isEmpty())
    }

    @Test
    fun neonThemeDoesNotRestyleTheControlsDrawnOverTheGame() {
        val controls = sourceRoot().resolve("ui/common/VectorTouchControls.kt").readText()

        assertFalse("Game controls must keep their selected visual style", "ui.theme.neon" in controls)
        assertTrue("Game controls must retain their own rounded geometry", "RoundedCornerShape" in controls)
    }

    @Test
    fun materialButtonsAndChipsAlwaysUseTheNeonShapeAdapters() {
        val root = sourceRoot()
        val neonImplementation = root.resolve("ui/theme/neon")
        val componentNames = listOf(
            "Button",
            "OutlinedButton",
            "FilledTonalButton",
            "ElevatedButton",
            "FilterChip",
            "AssistChip",
            "InputChip",
            "SuggestionChip"
        )
        val offenders = mutableListOf<String>()

        Files.walk(root.resolve("ui")).use { paths ->
            paths
                .filter { it.toString().endsWith(".kt") && !it.startsWith(neonImplementation) }
                .forEach { file ->
                    val source = file.readText()
                    componentNames.forEach { component ->
                        balancedCalls(source, component).forEach { call ->
                            if (!Regex("""\bshape\s*=""").containsMatchIn(call)) {
                                offenders += "${root.relativize(file)}: $component"
                            }
                        }
                    }
                }
        }

        assertTrue("Material controls bypass Neon shape adapters: $offenders", offenders.isEmpty())
    }

    @Test
    fun settingsReuseTheExactThreeColorCornerAccentCycleFromEmuCoreX() {
        val root = sourceRoot()
        val settings = root.resolve("ui/settings/SettingsScreen.kt").readText()
        val tabContent = root.resolve("ui/settings/SettingsTabContent.kt").readText()

        assertTrue("Settings rows must cycle all three X accents", "neonAccentColor(title.hashCode().mod(3))" in settings)
        assertTrue("Settings rows must use X corner marks", ".neonCornerAccent(" in settings)
        assertTrue("Secondary settings cards must use the same cycle", ".neonCornerAccent(" in tabContent)
    }

    @Test
    fun gameMenuSliderMetadataCannotCollapseItsLabelToOneCharacterPerLine() {
        val menu = sourceRoot().resolve("ui/emulation/EmulationMenu.kt").readText()
        val sliderRow = menu.substringAfter("private fun MenuSliderRow(").substringBefore("private fun RowBadge(")

        assertTrue(
            "Slider label and restart badge must share the flexible column",
            "modifier = Modifier.weight(1f)" in sliderRow && "RowBadge(badge)" in sliderRow
        )
        assertTrue(
            "The trailing value must be capped so it cannot consume the sidebar width",
            "modifier = Modifier.widthIn(max = 140.dp)" in sliderRow
        )
    }

    @Test
    fun customizationPreviewKeepsTheBadgeOnTheAppTitleRow() {
        val preview = sourceRoot()
            .resolve("ui/settings/CustomizationTab.kt")
            .readText()
            .substringAfter("private fun CustomizationPreview(")
            .substringBefore("private fun ThemeChip(")

        val title = preview.indexOf("R.string.app_name_emucorev")
        val badge = preview.indexOf("R.plurals.customization_games_per_row")
        val subtitle = preview.indexOf("R.string.customization_preview_subtitle")
        assertTrue("Preview title must precede its badge", title in 0 until badge)
        assertTrue("Preview subtitle must be below the title/badge row", badge in 0 until subtitle)
        assertTrue("Preview subtitle must receive the full row width", "modifier = Modifier.fillMaxWidth()" in preview.substring(subtitle).take(300))
    }

    @Test
    fun proSupportDialogUsesTheStyledLayoutOnlyForNeon() {
        val source = sourceRoot().resolve("ui/pro/ProPurchaseUi.kt").readText()

        assertTrue(
            "Neon must use the dedicated responsive support dialog",
            "if (LocalNeonTheme.current)" in source &&
                "NeonProSupportDialog(" in source &&
                "widthIn(max = 560.dp)" in source &&
                "heightIn(max = maxHeight)" in source
        )
        assertTrue(
            "Other themes must keep the existing Material dialog",
            "AlertDialog(" in source
        )
        assertFalse(
            "Neon tier cards must not expose the verbose Play product title",
            "text = offer.title" in source.substringAfter("private fun NeonSupportOfferCard(")
        )
    }

    @Test
    fun everyLocaleContainsTheStyledProSupportDialogCopy() {
        val expected = setOf(
            "settings_pro_support_dialog_eyebrow",
            "settings_pro_support_dialog_title",
            "settings_pro_support_dialog_desc",
            "settings_pro_supporter_title",
            "settings_pro_supporter_desc",
            "settings_pro_patron_title",
            "settings_pro_patron_desc",
            "settings_pro_support_same_features"
        )
        Files.list(resourceRoot()).use { paths ->
            paths
                .filter { it.fileName.toString() == "values" || it.fileName.toString().startsWith("values-") }
                .map { it.resolve("strings.xml") }
                .filter(Files::isRegularFile)
                .forEach { file ->
                    val document = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(file.toFile())
                    val nodes = document.getElementsByTagName("string")
                    val names = (0 until nodes.length).mapNotNull { index ->
                        nodes.item(index).attributes?.getNamedItem("name")?.nodeValue
                    }.toSet()
                    assertTrue("Missing Pro support copy in ${file.parent.fileName}: ${expected - names}", names.containsAll(expected))
                }
        }
    }

    private fun balancedCalls(source: String, component: String): List<String> {
        val startPattern = Regex("""\b${Regex.escape(component)}\s*\(""")
        return startPattern.findAll(source).mapNotNull { match ->
            val open = source.indexOf('(', match.range.first)
            var depth = 0
            var inString = false
            var escaped = false
            for (index in open until source.length) {
                val char = source[index]
                if (inString) {
                    if (escaped) escaped = false
                    else if (char == '\\') escaped = true
                    else if (char == '"') inString = false
                    continue
                }
                if (char == '"') {
                    inString = true
                } else if (char == '(') {
                    depth++
                } else if (char == ')') {
                    depth--
                    if (depth == 0) return@mapNotNull source.substring(match.range.first, index + 1)
                }
            }
            null
        }.toList()
    }

    private fun String.countOccurrences(token: String): Int = split(token).size - 1

    private fun sourceRoot(): Path {
        val workingDirectory = Path.of(System.getProperty("user.dir"))
        val appModule = sequenceOf(workingDirectory, workingDirectory.resolve("app"))
            .firstOrNull { Files.isDirectory(it.resolve("src/main")) }
            ?: error("Unable to locate Android app module from $workingDirectory")
        return appModule.resolve("src/main/java/com/sbro/emucorev")
    }

    private fun resourceRoot(): Path {
        val workingDirectory = Path.of(System.getProperty("user.dir"))
        return sequenceOf(
            workingDirectory.resolve("src/main/res"),
            workingDirectory.resolve("app/src/main/res")
        ).firstOrNull(Path::isDirectory)
            ?: error("Unable to locate app/src/main/res from $workingDirectory")
    }
}
