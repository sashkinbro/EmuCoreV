package com.sbro.emucorev.core

import com.sbro.emucorev.core.LiveGraphicsSettings.Option
import java.nio.file.Files
import java.nio.file.Path
import javax.xml.parsers.DocumentBuilderFactory
import kotlin.io.path.readText
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class LiveGraphicsSettingsTest {
    private val app = sequenceOf(Path.of(System.getProperty("user.dir")), Path.of(System.getProperty("user.dir"), "app"))
        .first { Files.isDirectory(it.resolve("src/main")) }

    private fun liveOptions(config: VitaCoreConfig, nativeLimit: Float) =
        LiveGraphicsSettings.liveOptions(LiveGraphicsSettings.capabilities(config, nativeLimit))

    @Test
    fun vulkanSessionOffersResolutionAndHidesVsync() {
        val options = liveOptions(VitaCoreConfig(backendRenderer = "Vulkan"), 4f)
        assertTrue(Option.RESOLUTION in options)
        assertTrue(Option.SCREEN_FILTER in options)
        assertTrue(Option.FRAME_LIMIT in options)
        // the Vulkan swapchain picks its own present mode, so V-Sync would be a dead switch
        assertFalse(Option.V_SYNC in options)
    }

    @Test
    fun openGlSessionHasNoLiveResolutionButKeepsVsync() {
        // the native side reports 0 for anything that is not a running Vulkan session
        val options = liveOptions(VitaCoreConfig(backendRenderer = "OpenGL"), 0f)
        assertFalse(Option.RESOLUTION in options)
        assertTrue(Option.V_SYNC in options)
        assertFalse(Option.TURBO_MODE in options)
    }

    @Test
    fun openGlNeverOffersResolutionEvenIfTheCoreReportedALimit() {
        assertFalse(Option.RESOLUTION in liveOptions(VitaCoreConfig(backendRenderer = "OpenGL"), 8f))
    }

    @Test
    fun resolutionIsHiddenWhenTheCoreCannotSwitch() {
        assertFalse(Option.RESOLUTION in liveOptions(VitaCoreConfig(backendRenderer = "Vulkan"), 0f))
    }

    @Test
    fun turboModeNeedsACustomDriver() {
        assertFalse(Option.TURBO_MODE in liveOptions(VitaCoreConfig(backendRenderer = "Vulkan"), 4f))
        assertTrue(
            Option.TURBO_MODE in liveOptions(
                VitaCoreConfig(backendRenderer = "Vulkan", customDriverName = "turnip.zip"), 4f
            )
        )
    }

    @Test
    fun setupOnlyOptionsHaveNoLiveCounterpart() {
        val live = Option.values().map { it.name }.toSet()
        LiveGraphicsSettings.SetupOnlyOption.values().forEach { assertFalse(it.name in live) }
    }

    @Test
    fun resolutionChoicesFollowTheHalfStepGridUpToTheLimit() {
        assertEquals(listOf(0.5f, 1f, 1.5f, 2f), LiveGraphicsSettings.resolutionChoices(2f))
        assertEquals(16, LiveGraphicsSettings.resolutionChoices(8f).size)
        assertEquals(8f, LiveGraphicsSettings.resolutionChoices(64f).last())
        assertEquals(emptyList<Float>(), LiveGraphicsSettings.resolutionChoices(0f))
        assertEquals(listOf(0.5f, 1f, 1.5f, 2f), LiveGraphicsSettings.resolutionChoices(2.2f))
    }

    @Test
    fun normalizeAcceptsOnlyGridValuesWithinTheLimit() {
        assertEquals(2f, LiveGraphicsSettings.normalizeResolution(2f, 4f))
        assertEquals(1.5f, LiveGraphicsSettings.normalizeResolution(1.5f, 4f))
        assertNull(LiveGraphicsSettings.normalizeResolution(1.25f, 4f))
        assertNull(LiveGraphicsSettings.normalizeResolution(5f, 4f))
        assertNull(LiveGraphicsSettings.normalizeResolution(0.25f, 4f))
        assertNull(LiveGraphicsSettings.normalizeResolution(2f, 0f))
    }

    @Test
    fun resolutionLabels() {
        assertEquals("1x", LiveGraphicsSettings.formatResolution(1f))
        assertEquals("1.5x", LiveGraphicsSettings.formatResolution(1.5f))
    }

    private class Recorder {
        val calls = mutableListOf<String>()
        var coreAccepts = true
    }

    private fun switch(recorder: Recorder) = LiveResolutionSwitch(
        ensurePaused = { recorder.calls += "pause" },
        applyToCore = { value ->
            recorder.calls += "apply:$value"
            recorder.coreAccepts
        },
        persist = { value -> recorder.calls += "persist:$value" }
    )

    @Test
    fun switchPausesAppliesThenPersistsInThatOrder() = runBlocking {
        val recorder = Recorder()
        val outcome = switch(recorder).switchTo(current = 1f, requested = 2f, limit = 4f)
        assertEquals(LiveResolutionSwitch.Outcome.APPLIED, outcome)
        assertEquals(listOf("pause", "apply:2.0", "persist:2.0"), recorder.calls)
    }

    @Test
    fun switchBackAndForthKeepsStoredValueEqualToRunningValue() = runBlocking {
        val recorder = Recorder()
        val sequence = switch(recorder)
        var stored = 1f
        for (target in listOf(2f, 1f)) {
            if (sequence.switchTo(stored, target, 4f) == LiveResolutionSwitch.Outcome.APPLIED) stored = target
        }
        assertEquals(1f, stored)
        assertEquals(
            listOf("pause", "apply:2.0", "persist:2.0", "pause", "apply:1.0", "persist:1.0"),
            recorder.calls
        )
    }

    @Test
    fun refusedByCoreIsNeverPersisted() = runBlocking {
        val recorder = Recorder().apply { coreAccepts = false }
        val outcome = switch(recorder).switchTo(1f, 2f, 4f)
        assertEquals(LiveResolutionSwitch.Outcome.FAILED, outcome)
        assertEquals(listOf("pause", "apply:2.0"), recorder.calls)
    }

    @Test
    fun unchangedAndInvalidValuesTouchNothing() = runBlocking {
        val recorder = Recorder()
        assertEquals(LiveResolutionSwitch.Outcome.UNCHANGED, switch(recorder).switchTo(2f, 2f, 4f))
        assertEquals(LiveResolutionSwitch.Outcome.REJECTED, switch(recorder).switchTo(2f, 3.25f, 4f))
        assertEquals(LiveResolutionSwitch.Outcome.REJECTED, switch(recorder).switchTo(2f, 6f, 4f))
        assertTrue(recorder.calls.isEmpty())
    }

    @Test
    fun inGameMenuHasNoRestartOrConnectionBadges() {
        val menu = app.resolve("src/main/java/com/sbro/emucorev/ui/emulation/EmulationMenu.kt").readText()
        assertFalse(menu.contains("MenuBadge.Restart"))
        assertFalse(menu.contains("MenuBadge.Live"))
        assertFalse(menu.contains("badge_restart"))
        assertFalse(menu.contains("badge_online"))
        assertFalse(menu.contains("MenuBadge.Online"))
    }

    @Test
    fun everyLocaleDropsTheBadgeStringsAndHasTheFailureMessage() {
        val resources = app.resolve("src/main/res")
        val directories = Files.list(resources).use { paths ->
            paths.filter {
                val name = it.fileName.toString()
                name == "values" || (name.startsWith("values-") && name != "values-night")
            }.toList()
        }
        assertEquals(18, directories.size)
        directories.forEach { directory ->
            val document = DocumentBuilderFactory.newInstance().newDocumentBuilder()
                .parse(directory.resolve("strings.xml").toFile())
            val nodes = document.getElementsByTagName("string")
            val keys = (0 until nodes.length)
                .mapNotNull { nodes.item(it).attributes?.getNamedItem("name")?.nodeValue }
                .toSet()
            assertTrue("failure message missing in ${directory.fileName}", "emulation_menu_resolution_failed" in keys)
            assertFalse("restart badge string left in ${directory.fileName}", "emulation_menu_badge_restart" in keys)
            assertFalse("live badge string left in ${directory.fileName}", "emulation_menu_badge_live" in keys)
        }
    }

    @Test
    fun jniSymbolsMatchKotlinExternals() {
        val native = app.resolve("src/main/cpp/emucorev/src/runtime_bridge.cpp").readText()
        val emulator = app.resolve("src/main/java/com/sbro/emucorev/core/vita/Emulator.kt").readText()
        listOf("getLiveResolutionLimit", "setLiveResolutionMultiplier", "setLiveScreenFilter").forEach { name ->
            assertTrue(native.contains("Java_com_sbro_emucorev_core_vita_Emulator_$name"))
            assertTrue(emulator.contains("external fun $name"))
        }
    }
}
