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
        val gameMenuPicker = root.resolve("ui/settings/GameMenuCustomizationTab.kt").readText()
        val overlay = root.resolve("ui/emulation/EmulationOverlay.kt").readText()
        val menu = root.resolve("ui/emulation/EmulationMenu.kt").readText()
        val drawer = root.resolve("navigation/AdaptiveShell.kt").readText()

        assertTrue("Touch style picker must enumerate every preset", "TouchControlVisualStyle.entries" in pickers)
        assertTrue("Press effect picker must enumerate every effect", "TouchControlPressEffect.entries" in pickers)
        assertTrue("Drawer picker must enumerate every appearance", "DrawerVisualStyle.entries" in pickers)
        assertTrue("Game menu picker must enumerate every layout", "GameMenuLayoutStyle.entries" in gameMenuPicker)
        assertTrue("Touch styles must reach the runtime overlay", "customization.touchControlVisualStyle" in overlay)
        assertTrue("Press effects must reach the runtime overlay", "customization.touchControlPressEffect" in overlay)
        assertTrue("Menu layout must reach the runtime menu", "layoutStyle = customization.gameMenuLayoutStyle" in overlay)
        assertTrue("Runtime menu must branch over all layouts", "when (effectiveStyle)" in menu)
        assertTrue("Drawer must use its selected appearance", "LocalDrawerVisualStyle provides drawerVisualStyle" in drawer)
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

    private fun sourceRoot(): Path {
        val workingDirectory = Path.of(System.getProperty("user.dir"))
        val appModule = sequenceOf(workingDirectory, workingDirectory.resolve("app"))
            .firstOrNull { Files.isDirectory(it.resolve("src/main")) }
            ?: error("Unable to locate Android app module from $workingDirectory")
        return appModule.resolve("src/main/java/com/sbro/emucorev")
    }
}
