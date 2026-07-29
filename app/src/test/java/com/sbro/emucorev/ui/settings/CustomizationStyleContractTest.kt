package com.sbro.emucorev.ui.settings

import com.sbro.emucorev.data.CustomizationSettings
import com.sbro.emucorev.data.DrawerVisualStyle
import com.sbro.emucorev.data.GameMenuLayoutStyle
import com.sbro.emucorev.data.TouchControlPressEffect
import com.sbro.emucorev.data.TouchControlVisualStyle
import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.readText
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class CustomizationStyleContractTest {
    @Test
    fun defaultsPreserveTheExistingLookAndInputBehavior() {
        val settings = CustomizationSettings()
        assertEquals(TouchControlVisualStyle.CLASSIC, settings.touchControlVisualStyle)
        assertEquals(TouchControlPressEffect.GROW, settings.touchControlPressEffect)
        assertEquals(GameMenuLayoutStyle.SIDEBAR, settings.gameMenuLayoutStyle)
        assertEquals(DrawerVisualStyle.CLASSIC, settings.drawerVisualStyle)
    }

    @Test
    fun everyStyleIsShownInSettingsAndAppliedAtRuntime() {
        val root = sourceRoot()
        val pickers = root.resolve("ui/settings/CustomizationStylePickers.kt").readText()
        val customizationTab = root.resolve("ui/settings/CustomizationTab.kt").readText()
        val gameMenuPicker = root.resolve("ui/settings/GameMenuCustomizationTab.kt").readText()
        val vectorControls = root.resolve("ui/common/VectorTouchControls.kt").readText()
        val overlay = root.resolve("ui/emulation/EmulationOverlay.kt").readText()
        val menu = root.resolve("ui/emulation/EmulationMenu.kt").readText()
        val drawer = root.resolve("navigation/AdaptiveShell.kt").readText()

        assertTrue("Touch style picker must enumerate every preset", "TouchControlVisualStyle.entries" in pickers)
        assertTrue("Press effect picker must enumerate every effect", "TouchControlPressEffect.entries" in pickers)
        assertTrue("Drawer picker must enumerate every appearance", "DrawerVisualStyle.entries" in pickers)
        assertTrue("Game menu picker must enumerate every layout", "GameMenuLayoutStyle.entries" in gameMenuPicker)
        assertTrue(
            "Game menu picker must live next to the drawer picker in Customization",
            customizationTab.indexOf("GameMenuStyleSection(") >
                customizationTab.indexOf("DrawerStyleSection(")
        )
        assertTrue("Touch styles must reach the runtime overlay", "customization.touchControlVisualStyle" in overlay)
        assertTrue("Press effects must reach the runtime overlay", "customization.touchControlPressEffect" in overlay)
        assertTrue("Menu layout must reach the runtime menu", "layoutStyle = customization.gameMenuLayoutStyle" in overlay)
        assertTrue("Runtime menu must branch over all layouts", "when (effectiveStyle)" in menu)
        assertTrue("Drawer must use its selected appearance", "LocalDrawerVisualStyle provides drawerVisualStyle" in drawer)
        assertTrue("Settings preview must use the real vector analog", "VectorAnalogStick(" in pickers)
        assertTrue("Settings preview must use the real vector buttons", "VectorOverlayButton(" in pickers)
        assertTrue("Runtime analog must use the shared vector renderer", "VectorAnalogStick(" in overlay)
        assertTrue("Runtime buttons must use the shared vector renderer", "VectorOverlayButton(" in overlay)
        assertTrue("Vector profiles must provide distinct button brushes", "val styleBrush = when (visualStyle)" in vectorControls)
        assertTrue("Vector profiles must provide distinct analog rendering", "TouchControlVisualStyle.ARCADE ->" in vectorControls)
    }

    @Test
    fun horizontalStyleListsOwnTheirEdgePadding() {
        val root = sourceRoot()
        val drawerPicker = root.resolve("ui/settings/CustomizationStylePickers.kt").readText()
        val gameMenuPicker = root.resolve("ui/settings/GameMenuCustomizationTab.kt").readText()

        assertTrue("Drawer styles must use a lazy horizontal list", "LazyRow(" in drawerPicker)
        assertTrue(
            "Drawer list padding must scroll together with its content",
            "contentPadding = PaddingValues(horizontal = 14.dp" in drawerPicker
        )
        assertTrue("Game menu styles must use a lazy horizontal list", "LazyRow(" in gameMenuPicker)
        assertTrue(
            "Game menu list padding must scroll together with its content",
            "contentPadding = PaddingValues(horizontal = 14.dp" in gameMenuPicker
        )
        assertTrue(
            "Touch style and press-effect lists must both own their edge padding",
            Regex("""contentPadding = PaddingValues\(horizontal = 14\.dp""")
                .findAll(drawerPicker)
                .count() >= 3
        )
        assertTrue(
            "Customization must not retain manually clipped horizontal rows",
            ".horizontalScroll(" !in drawerPicker
        )
    }

    @Test
    fun importedBackgroundHasAnImmediateVisibleSelectedState() {
        val root = sourceRoot()
        val customization = root.resolve("ui/settings/CustomizationTab.kt").readText()
        val preferences = root.resolve("data/CustomizationPreferences.kt").readText()

        assertTrue("Selected background must show a success icon", "Icons.Rounded.CheckCircle" in customization)
        assertTrue("Selected background must show its imported file name", "File(it).name" in customization)
        assertTrue("Background card must expose selected styling", "selected = settings.backgroundPath != null" in customization)
        assertTrue("Background state must refresh immediately after import", "_settings.value = readSettings()" in preferences)
    }

    @Test
    fun visualEffectsDoNotChangeTheTouchTargetGeometry() {
        val overlay = sourceRoot().resolve("ui/emulation/EmulationOverlay.kt").readText()
        val item = Regex(
            """val sizeModifier = Modifier[\s\S]*?Box\(modifier = sizeModifier\.then\(inputModifier\)[\s\S]*?when \(descriptor\.type\)"""
        ).find(overlay)?.value.orEmpty()

        assertTrue("Touch target geometry and input modifier must remain the outer layer", item.isNotBlank())
        assertTrue("Visual scaling must use graphicsLayer", "graphicsLayer(" in overlay)
        assertTrue("All press effects need explicit scale behavior", "TouchControlPressEffect.GLOW -> 1.02f" in overlay)
    }

    @Test
    fun defaultDpadButtonsHaveARealGapAndDoNotOverlap() {
        val overlay = sourceRoot().resolve("ui/emulation/EmulationOverlay.kt").readText()
        val clusterSize = 136f
        val buttonSize = clusterSize / 3.25f
        val axisStep = (clusterSize - buttonSize) / 2f

        assertTrue("D-pad buttons need a positive gap", axisStep - buttonSize > 0f)
        assertTrue("D-pad button size must use the separated 3x3 layout", "dpadClusterSize / 3.25f" in overlay)
        assertTrue("Down must occupy the bottom cell", "dpadY + dpadStep * 2f" in overlay)
        assertTrue("Right must occupy the trailing cell", "sidePaddingPx + dpadStep * 2f" in overlay)
    }

    private fun sourceRoot(): Path {
        val workingDirectory = Path.of(System.getProperty("user.dir"))
        val appModule = sequenceOf(workingDirectory, workingDirectory.resolve("app"))
            .firstOrNull { Files.isDirectory(it.resolve("src/main")) }
            ?: error("Unable to locate Android app module from $workingDirectory")
        return appModule.resolve("src/main/java/com/sbro/emucorev")
    }
}
