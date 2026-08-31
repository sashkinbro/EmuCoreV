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
    fun nativeTouchInputRejectsInvalidCoordinatesAndReleasesExactPointers() {
        val touch = appModule()
            .resolve("src/main/cpp/vita3k/vita3k/touch/src/touch.cpp")
            .readText()

        assertTrue("SDL cancellation must release a stranded touch", "SDL_EVENT_FINGER_CANCELED" in touch)
        assertTrue("Letterbox touches must be validated", "normalized_touch_to_report" in touch)
        assertTrue("An invalid viewport must not be divided by", "viewport_w <= 0" in touch)
        assertTrue("Unknown up events must not remove another finger", touch.split("finger_index < 0").size >= 3)
        assertTrue("Overflow must preserve existing touches", "finger_count >= SCE_TOUCH_MAX_REPORT" in touch)
    }

    @Test
    fun frontBackAndBothModesUseTheSameIntegerJniContract() {
        val app = appModule()
        val root = sourceRoot()
        val overlay = root.resolve("ui/emulation/EmulationOverlay.kt").readText()
        val bridge = root.resolve("core/vita/overlay/InputOverlay.kt").readText()
        val nativeBridge = app.resolve("src/main/cpp/emucorev/src/input_overlay_bridge.cpp").readText()
        val nativeInput = app.resolve("src/main/cpp/vita3k/vita3k/android/jni/input_overlay.cpp").readText()
        val touch = app.resolve("src/main/cpp/vita3k/vita3k/touch/src/touch.cpp").readText()

        assertTrue("The switch must cycle front/back/both", "(touchMode + 1) % 3" in overlay)
        assertTrue("Kotlin must not narrow the mode to Boolean", "setTouchState(mode: Int)" in bridge)
        assertTrue("Our JNI bridge must preserve all three modes", "jint" in nativeBridge)
        assertTrue("Core JNI must accept an integer mode", "jint" in nativeInput)
        assertTrue("Both mode must be explicit", "state.touchscreen_both = (mode == 2)" in touch)
        assertTrue("Both panels must receive separate coordinate conversion", "recover_touch_events(emuenv, touch_port)" in touch)
        assertTrue("Both panels must honor their own force setting", "touch.force_touch_enabled[port]" in touch)
    }

    @Test
    fun memoryPressureCallbacksDoNotDereferenceSessionObjects() {
        val nativeRoot = appModule().resolve("src/main/cpp/vita3k/vita3k")
        val bootstrap = nativeRoot.resolve("android/jni/native_bootstrap.cpp").readText()
        val callback = bootstrap.substringAfter("NativeLib_onTrimMemory(")
            .substringBefore("JNIEXPORT")
        val renderer = nativeRoot.resolve("renderer/src/vulkan/renderer.cpp").readText()

        assertTrue("Trim must use the process-lifetime mailbox", "mem_diag::pending_trim_level" in callback)
        assertTrue("A session may be destroyed while Android requests trim", "session." !in callback)
        assertTrue("GPU trim must be consumed on the render thread", "pending_trim_level.exchange" in renderer)
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
