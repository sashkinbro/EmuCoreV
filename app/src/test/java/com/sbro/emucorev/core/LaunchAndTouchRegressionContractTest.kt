package com.sbro.emucorev.core

import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.readText
import org.junit.Assert.*
import org.junit.Test

class LaunchAndTouchRegressionContractTest {
    private val app = sequenceOf(Path.of(System.getProperty("user.dir")), Path.of(System.getProperty("user.dir"), "app"))
        .first { Files.isDirectory(it.resolve("src/main")) }
    private fun kotlin(path: String) = app.resolve("src/main/java/com/sbro/emucorev/$path").readText()
    private fun native(path: String) = app.resolve("src/main/cpp/vita3k/$path").readText()

    @Test fun launchPreparationIsOffMainThreadAndActivityDispatchReturnsToMain() {
        val launch = kotlin("core/VitaLaunchBridge.kt")
        assertTrue(launch.contains("suspend fun launchInstalledTitle"))
        assertTrue(launch.indexOf("withContext(Dispatchers.IO)") < launch.indexOf("hasInstalledFirmware(context)"))
        assertTrue(launch.contains("withContext(Dispatchers.Main.immediate) { context.startActivity(intent) }"))
        assertTrue(launch.contains("launchInFlight.compareAndSet(false, true)"))
        assertTrue(launch.contains("catch (cancelled: CancellationException)"))
        assertTrue(launch.substringAfter("finally {").contains("launchInFlight.set(false)"))
        val launchOnly = launch.substringAfter("private suspend fun launchWithArgs").substringBefore("private fun runWithArgs")
        assertFalse(launchOnly.contains("refreshAppsList()"))
    }

    @Test fun missingNativeAppStillRefreshesTheListOnDemand() {
        val setup = native("vita3k/app/src/app.cpp").substringAfter("bool setup_game_launch").substringBefore("void prepare_game_launch_overlay")
        assertTrue(setup.indexOf("if (!set_app_info") < setup.indexOf("scan_apps"))
        assertTrue(setup.contains("!scan_apps(emuenv) || !set_app_info(emuenv, app_path)"))
        val lookup = kotlin("data/InstalledGameRepository.kt").substringAfter("fun findByTitleId").substringBefore("fun deleteByTitleId")
        assertTrue(lookup.indexOf("File(EmulatorStorage.ux0AppRoot") < lookup.indexOf("loadInstalledGames"))
        assertTrue(lookup.contains("takeIf(::isSafePathSegment)"))
    }

    @Test fun pauseAndLostFocusImmediatelyCancelTheWholeMultitouchStream() {
        val activity = kotlin("core/vita/Emulator.kt")
        val pause = activity.substringAfter("override fun onPause()").substringBefore("override fun onDestroy()")
        assertTrue(pause.indexOf("cancelActiveTouches()") < pause.indexOf("super.onPause()"))
        val focus = activity.substringAfter("override fun onWindowFocusChanged").substringBefore("override fun")
        assertTrue(focus.contains("if (!hasFocus) cancelActiveTouches()"))
        val cancel = activity.substringAfter("private fun cancelActiveTouches()").substringBefore("private fun attachComposeOverlay()")
        assertTrue(cancel.contains("event.action = MotionEvent.ACTION_CANCEL"))
        assertTrue(cancel.contains("super.dispatchTouchEvent(event)"))
        assertTrue(cancel.contains("event.recycle()"))
        val overlay = kotlin("ui/emulation/EmulationOverlay.kt")
        assertTrue(overlay.contains("DisposableEffect(buttonTracker)"))
        assertTrue(overlay.contains("if (showTouchControls && inputResumed)"))
    }

    @Test fun virtualTriggerReleaseMatchesTheBundledSdlAxisRange() {
        val overlay = native("vita3k/android/jni/input_overlay.cpp")
        assertTrue(overlay.contains("value ? SDL_MAX_SINT16 : SDL_MIN_SINT16"))
        val sdl = native("external/sdl/src/joystick/virtual/SDL_virtualjoystick.c")
        assertTrue(sdl.contains("hwdata->axes[axis_triggerleft] = SDL_JOYSTICK_AXIS_MIN"))
        assertTrue(sdl.contains("hwdata->axes[axis_triggerright] = SDL_JOYSTICK_AXIS_MIN"))
    }

    @Test fun nativeBufferKeepsAtomicAndNoncoherentSafetyChecks() {
        val renderer = native("vita3k/renderer/src/vulkan/renderer.cpp")
        val nativeBuffer = renderer.substringAfter("case MappingMethod::NativeBuffer:").substringBefore("case MappingMethod::PageTable:")
        val allocation = nativeBuffer.substringAfter("AHardwareBuffer_Desc")
        assertTrue(allocation.contains("test_arm64_atomics_on(mapped_location)"))
        assertTrue(allocation.contains("add_external_mapping(mem"))
        assertTrue(allocation.indexOf("test_arm64_atomics_on(mapped_location)") < allocation.indexOf("add_external_mapping(mem"))
        assertTrue(nativeBuffer.contains("if (mapped_memory_type < 0)"))
        assertTrue(renderer.contains("test_arm64_atomics_on(buffer.mapped_data)"))
        assertTrue(renderer.contains("native_buffer_supported(support_memory_mapping, support_android_buffer_import, support_unix_fd_import)"))
    }
}
