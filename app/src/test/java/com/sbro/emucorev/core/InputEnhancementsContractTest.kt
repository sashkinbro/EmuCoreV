package com.sbro.emucorev.core

import java.nio.file.Files
import java.nio.file.Path
import javax.xml.parsers.DocumentBuilderFactory
import kotlin.io.path.isDirectory
import kotlin.io.path.readText
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class InputEnhancementsContractTest {
    @Test
    fun defaultsAndPresetRangesMatchTheRuntimeContract() {
        val defaults = VitaCoreConfig()

        assertTrue(defaults.touchHaptics)
        assertEquals(VitaCoreConfig.TOUCH_HAPTICS_PRESET_BALANCED, defaults.touchHapticsPreset)
        assertEquals(60, defaults.touchHapticsStrength)
        assertEquals(VitaCoreConfig.GYRO_MODE_OFF, defaults.gyroMode)
        assertEquals(100, defaults.gyroSensitivity)
        assertEquals(45, defaults.gyroSmoothing)
        assertEquals(0, VitaCoreConfig.TOUCH_HAPTICS_PRESET_SOFT)
        assertEquals(3, VitaCoreConfig.TOUCH_HAPTICS_PRESET_STRONG)
        assertEquals(1, VitaCoreConfig.GYRO_MODE_AIM)
        assertEquals(2, VitaCoreConfig.GYRO_MODE_STEERING)
    }

    @Test
    fun globalBackupAndPerGameRepositoriesPersistEveryInputField() {
        val root = sourceRoot()
        val global = root.resolve("core/VitaCoreConfigRepository.kt").readText()
        val backup = root.resolve("core/SettingsBackupRepository.kt").readText()
        val perGame = root.resolve("core/VitaGameSettingsRepository.kt").readText()
        val fields = listOf(
            "touchHaptics",
            "touchHapticsPreset",
            "touchHapticsStrength",
            "gyroMode",
            "gyroSensitivity",
            "gyroSmoothing",
            "gyroInvertX",
            "gyroInvertY"
        )
        val persistedKeys = listOf(
            "touch-haptics",
            "touch-haptics-preset",
            "touch-haptics-strength",
            "gyro-mode",
            "gyro-sensitivity",
            "gyro-smoothing",
            "gyro-invert-x",
            "gyro-invert-y"
        )

        fields.forEach { field ->
            assertTrue("$field must be included in settings backup", field in backup)
        }
        persistedKeys.forEach { key ->
            assertTrue("$key must be loaded and saved globally", global.split(key).size >= 4)
            assertTrue("$key must be loaded and saved per game", perGame.split(key).size >= 3)
        }
    }

    @Test
    fun runtimeUsesLifecycleSafeGyroAndImmediateStickMotion() {
        val root = sourceRoot()
        val overlay = root.resolve("ui/emulation/EmulationOverlay.kt").readText()
        val vectorControls = root.resolve("ui/common/VectorTouchControls.kt").readText()
        val gyro = root.resolve("core/AndroidGyroscopeInput.kt").readText()

        assertTrue("Gyroscope must feed native analog axes", "overlayBridge.setAxis(axisX" in overlay)
        assertTrue("Gyroscope must stop while paused", "effectivePaused" in overlay)
        assertTrue("Gyroscope must stop on lifecycle pause", "Lifecycle.Event.ON_PAUSE" in overlay)
        assertTrue("Stopping gyro must release its stick", "onAnalog(mode, 0f, 0f)" in gyro)
        assertTrue("Analog sticks must consume raw MOVE events", "MotionEvent.ACTION_MOVE" in overlay)
        assertTrue(
            "Externally driven analog visuals must bypass delayed animation",
            "!interactive -> visualThumbOffset" in vectorControls
        )
    }

    @Test
    fun touchHapticsAreDispatchedOnlyOnButtonStateTransitions() {
        val overlay = sourceRoot().resolve("ui/emulation/EmulationOverlay.kt").readText()

        assertTrue("Touch haptics must reach runtime controls", "AndroidTouchHaptics.playButton(" in overlay)
        assertTrue("Duplicate pointer events must not duplicate haptics", "if (pressed != wasPressed)" in overlay)
        assertTrue("Press and release need different feedback", "ButtonPhase.PRESS else ButtonPhase.RELEASE" in overlay)
    }

    @Test
    fun everySupportedLocaleContainsTheInputEnhancementStrings() {
        val resourceRoot = resourceRoot()
        val requiredKeys = requiredKeys(resourceRoot.resolve("values/strings.xml"))
        val localizedDirectories = Files.list(resourceRoot).use { paths ->
            paths.filter {
                it.fileName.toString().startsWith("values-") &&
                    it.fileName.toString() != "values-night"
            }.toList()
        }

        assertEquals(11, localizedDirectories.size)
        assertTrue("Expected the complete input string set", requiredKeys.size >= 29)
        localizedDirectories.forEach { directory ->
            assertEquals(
                "Input resources differ in ${directory.fileName}",
                requiredKeys,
                requiredKeys(directory.resolve("strings.xml"))
            )
        }
    }

    private fun requiredKeys(path: Path): Set<String> {
        val document = DocumentBuilderFactory.newInstance()
            .newDocumentBuilder()
            .parse(path.toFile())
        val nodes = document.getElementsByTagName("string")
        return buildSet {
            for (index in 0 until nodes.length) {
                val name = nodes.item(index).attributes
                    ?.getNamedItem("name")
                    ?.nodeValue
                    .orEmpty()
                if (
                    name.startsWith("settings_touch_haptics") ||
                    name.startsWith("settings_help_touch_haptics") ||
                    name.startsWith("settings_gyro_") ||
                    name.startsWith("settings_help_gyro_") ||
                    name == "settings_touch_controls_section"
                ) add(name)
            }
        }
    }

    private fun sourceRoot(): Path = appModule().resolve("src/main/java/com/sbro/emucorev")

    private fun resourceRoot(): Path = appModule().resolve("src/main/res")

    private fun appModule(): Path {
        val workingDirectory = Path.of(System.getProperty("user.dir"))
        return sequenceOf(workingDirectory, workingDirectory.resolve("app"))
            .firstOrNull { Files.isDirectory(it.resolve("src/main")) }
            ?: error("Unable to locate Android app module from $workingDirectory")
    }
}
