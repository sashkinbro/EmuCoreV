package com.sbro.emucorev.data

import android.app.ActivityManager
import android.os.SystemClock
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.background
import androidx.compose.material3.MaterialTheme
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.Alignment
import androidx.compose.ui.unit.dp
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.test.core.app.ActivityScenario
import androidx.test.espresso.Espresso
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry
import androidx.test.runner.lifecycle.Stage
import com.sbro.emucorev.MainActivity
import com.sbro.emucorev.R
import com.sbro.emucorev.core.SaveStateBridge
import com.sbro.emucorev.core.VitaLaunchBridge
import com.sbro.emucorev.core.vita.Emulator
import com.sbro.emucorev.ui.shaders.RetroArchShaderFeaturePanel
import com.sbro.emucorev.ui.shaders.RetroArchShaderUiContext
import com.sbro.emucorev.ui.theme.EmuCoreVTheme
import com.sbro.emucorev.ui.theme.ThemeMode
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/** Real installed pack/device checks; official network update is explicitly opt-in. */
@RunWith(AndroidJUnit4::class)
class RetroArchShaderDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun largeBezelParametersStackVerticallyInASidebar() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val repository = RetroArchShaderRepository(context)
        assumeTrue(runBlocking { repository.hasInstalledOfficialPack() })
        val preferences = context.getSharedPreferences("retroarch_shader_selection", 0)
        val previous = preferences.all.toMap()
        try {
            val preset = runBlocking { repository.listPresets() }.first {
                it.label == "bezel/koko-aio/Presets-4.1/monitor-bloom"
            }
            assertTrue("Exercise a preset above the old 256-parameter limit", preset.parameters.size > 256)
            RetroArchShaderSelectionRepository(context).setGameOverride("PCSE00414", true, preset.absolutePath)
            compose.setContent { EmuCoreVTheme(themeMode = ThemeMode.DARK) {
                Box(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background), contentAlignment = Alignment.TopEnd) {
                    Column(Modifier.width(320.dp).verticalScroll(rememberScrollState())) {
                        RetroArchShaderFeaturePanel(RetroArchShaderUiContext.IN_GAME, "PCSE00414")
                    }
                }
            } }
            compose.waitUntil(30_000) {
                compose.onAllNodesWithTag("shader_parameter_search").fetchSemanticsNodes().isNotEmpty()
            }
            val parameter = preset.parameters.first { it.description.length > 45 && it.maximum > it.minimum }
            compose.onNodeWithTag("shader_parameter_search").performScrollTo().performTextInput(parameter.id)
            compose.onNodeWithTag("shader_parameter_list").performScrollTo()
            val label = compose.onNodeWithTag("shader_parameter_label:${parameter.id}").fetchSemanticsNode().boundsInRoot
            val value = compose.onNodeWithTag("shader_parameter_value:${parameter.id}").fetchSemanticsNode().boundsInRoot
            val slider = compose.onNodeWithTag("shader_parameter_slider:${parameter.id}").fetchSemanticsNode().boundsInRoot
            assertTrue("Value must be below its label", value.top >= label.bottom)
            assertTrue("Slider must be below its value", slider.top >= value.bottom)
            assertTrue("Label must occupy the sidebar width", label.width > 200)
            compose.onAllNodesWithText("too many parameters", substring = true).assertCountEquals(0)
            val image = compose.onRoot().captureToImage().asAndroidBitmap()
            java.io.File(context.cacheDir, "shader-parameter-layout.png").outputStream().use {
                image.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it)
            }
        } finally { restorePreferences(preferences, previous) }
    }

    @Test fun reopeningShaderTabKeepsItsSwitchAndCatalogReady() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val repository = RetroArchShaderRepository(context)
        assumeTrue(runBlocking { repository.hasInstalledOfficialPack() })
        val selections = RetroArchShaderSelectionRepository(context)
        val title = "PCSE00414"
        val preferences = context.getSharedPreferences("retroarch_shader_selection", 0)
        val previous = preferences.all.toMap()
        try {
            val preset = runBlocking { repository.listPresets() }.first { it.label == "scanlines/scanline" }
            selections.setGameOverride(title, true, preset.absolutePath)
            val visible = mutableStateOf(true)
            compose.setContent {
                EmuCoreVTheme {
                    Column(Modifier.verticalScroll(rememberScrollState())) {
                        if (visible.value) RetroArchShaderFeaturePanel(RetroArchShaderUiContext.IN_GAME, title)
                    }
                }
            }
            compose.waitUntil(30_000) {
                compose.onAllNodesWithTag("shader_pack_status").fetchSemanticsNodes().isNotEmpty()
            }
            repeat(5) {
                compose.runOnIdle { visible.value = false }
                compose.waitForIdle()
                compose.runOnIdle { visible.value = true }
                compose.onNodeWithTag("shader_catalog_loading").assertDoesNotExist()
                compose.onNode(isToggleable()).assertIsOn().assertIsEnabled()
                compose.onNodeWithText(context.getString(R.string.shader_redownload)).assertExists()
            }
            // External setting edits must still propagate without a catalog reload.
            selections.setGameOverride(title, false, preset.absolutePath)
            compose.waitUntil(5_000) {
                compose.onAllNodes(isToggleable() and isOff()).fetchSemanticsNodes().isNotEmpty()
            }
            compose.onNodeWithTag("shader_catalog_loading").assertDoesNotExist()
        } finally { restorePreferences(preferences, previous) }
    }

    @Test fun catalogAndPackStatusSurvivePanelChangesAndAnOfficialUpdate() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val repository = RetroArchShaderRepository(context)
        assumeTrue(runBlocking { repository.hasInstalledOfficialPack() })
        val initial = runBlocking { repository.listPresets() }
        assertTrue("Official catalog was truncated: ${initial.size}", initial.size > 2000)
        assertTrue("Cold-start runtime detection failed", runBlocking { repository.isSupported() })
        val visible = mutableStateOf(true)
        val inGame = mutableStateOf(false)
        compose.setContent {
            EmuCoreVTheme {
                Column(Modifier.verticalScroll(rememberScrollState())) {
                    if (visible.value) RetroArchShaderFeaturePanel(
                        context = if (inGame.value) RetroArchShaderUiContext.IN_GAME else RetroArchShaderUiContext.GLOBAL,
                        titleId = if (inGame.value) "PCSE00414" else null)
                }
            }
        }
        fun awaitCatalog() {
            compose.waitUntil(30_000) {
                compose.onAllNodesWithTag("shader_pack_status").fetchSemanticsNodes().isNotEmpty()
            }
            compose.onNodeWithText(context.getString(R.string.shader_redownload)).assertExists()
        }
        awaitCatalog()
        compose.onNodeWithTag("shader_preset_picker").performScrollTo().performClick()
        compose.onNode(hasSetTextAction()).performTextInput("scanline")
        assertTrue(compose.onAllNodesWithText("scanline", substring = true).fetchSemanticsNodes().size > 2)
        Espresso.pressBack()
        if (InstrumentationRegistry.getArguments().getString("downloadOfficial") == "true") {
            val before = RetroArchShaderPackTask.state.value.completion
            compose.onNodeWithTag("shader_download").performScrollTo().performClick()
            compose.waitUntil(30_000) {
                RetroArchShaderPackTask.state.value.progress?.let {
                    it.transferredBytes > 0 && it.bytesPerSecond > 0
                } == true
            }
            compose.onNodeWithTag("shader_transfer_details").assertExists()
            // The download must keep running while the user leaves the tab.
            compose.runOnIdle { visible.value = false }
            SystemClock.sleep(500)
            compose.runOnIdle { visible.value = true }
            compose.waitUntil(240_000) { RetroArchShaderPackTask.state.value.completion > before }
            assertNull(RetroArchShaderPackTask.state.value.error)
            assertTrue(runBlocking { RetroArchShaderRepository(context).hasInstalledOfficialPack() })
            assertTrue(runBlocking { RetroArchShaderRepository(context).listPresets().size } > 2000)
            awaitCatalog()
        }
        compose.runOnIdle { inGame.value = true }
        awaitCatalog()
        compose.onNodeWithText(context.getString(R.string.shader_scope_ingame)).assertExists()
    }

    @Test fun appliesAnOfficialPresetToARealGameAndRestoresUserSelection() {
        val title = InstrumentationRegistry.getArguments().getString("titleId")
        assumeTrue("Opt in with -e titleId <installed title>", !title.isNullOrBlank())
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val repository = RetroArchShaderRepository(context)
        assumeTrue(runBlocking { repository.hasInstalledOfficialPack() })
        val preferences = context.getSharedPreferences("retroarch_shader_selection", 0)
        val previous = preferences.all.toMap()
        try {
            val label = InstrumentationRegistry.getArguments().getString("shaderPreset") ?: "scanlines/scanline"
            val preset = runBlocking { repository.listPresets() }.first { it.label == label }
            RetroArchShaderSelectionRepository(context).setGameOverride(title!!, true, preset.absolutePath)
            ActivityScenario.launch(MainActivity::class.java).use { scenario ->
                lateinit var main: MainActivity
                scenario.onActivity { main = it }
                SystemClock.sleep(1500)
                assertEquals(VitaLaunchBridge.LaunchResult.Success,
                    runBlocking { VitaLaunchBridge.launchInstalledTitle(main, title) })
                SystemClock.sleep(500)
                main.getSystemService(ActivityManager::class.java).appTasks
                    .firstOrNull { it.taskInfo?.baseActivity?.className == Emulator::class.java.name }?.moveToFront()
                compose.waitUntil(30_000) { SaveStateBridge.runningTitleId() == title }
                assertTrue(runBlocking { RetroArchShaderSessionController(context).applyForTitle(title) { true } })
                SystemClock.sleep(5000)
                assertTrue("Shader runtime rejected the official preset: ${repository.lastApplyError()}",
                    repository.lastApplyError().isNullOrBlank())
                if (InstrumentationRegistry.getArguments().getString("inspectMenu") == "true") {
                    instrumentation.runOnMainSync {
                        ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED)
                            .filterIsInstance<Emulator>().first().openPauseMenuFromController()
                    }
                    compose.onNodeWithContentDescription(context.getString(R.string.shader_scope_ingame)).performClick()
                    compose.waitUntil(30_000) {
                        compose.onAllNodesWithTag("shader_parameter_search").fetchSemanticsNodes().isNotEmpty()
                    }
                    val parameter = preset.parameters.first { it.description.length > 45 && it.maximum > it.minimum }
                    compose.onNodeWithTag("shader_parameter_search").performScrollTo().performTextInput(parameter.id)
                    compose.onNodeWithTag("shader_parameter_list").performScrollTo().assertIsDisplayed()
                    instrumentation.uiAutomation.takeScreenshot()?.let { image ->
                        java.io.File(context.cacheDir, "shader-game-menu.png").outputStream().use {
                            image.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it)
                        }
                    }
                }
                instrumentation.runOnMainSync {
                    ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED)
                        .filterIsInstance<Emulator>().first().exitEmulation(autoSave = false)
                }
                compose.waitUntil(30_000) { SaveStateBridge.runningTitleId().isEmpty() }
            }
        } finally {
            restorePreferences(preferences, previous)
            RetroArchShaderSessionController(context).stop()
        }
    }

    private fun restorePreferences(preferences: android.content.SharedPreferences, previous: Map<String, *>) {
        val editor = preferences.edit().clear()
        previous.forEach { (key, value) -> when (value) {
                is Boolean -> editor.putBoolean(key, value)
                is String -> editor.putString(key, value)
                is Float -> editor.putFloat(key, value)
                is Int -> editor.putInt(key, value)
                is Long -> editor.putLong(key, value)
                is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
        } }
        editor.commit()
    }
}
