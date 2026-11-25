package com.sbro.emucorev.core

import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.readText
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Source-level guards for fixes that live in Compose/JNI code the JVM cannot execute. */
class MenuAndRearTouchContractTest {
    private val app = sequenceOf(Path.of(System.getProperty("user.dir")), Path.of(System.getProperty("user.dir"), "app"))
        .first { Files.isDirectory(it.resolve("src/main")) }

    private fun kotlin(path: String) = app.resolve("src/main/java/com/sbro/emucorev/$path").readText()
    private fun core(path: String) = app.resolve("src/main/cpp/emucorev/ECV/ECV/$path").readText()

    @Test
    fun touchJniNeverWaitsOnTheSaveStateMutex() {
        val source = core("android/jni/input_overlay.cpp")
        assertTrue(source.contains("std::try_to_lock"))
        assertTrue(source.contains("apply_pending_touch_commands"))
        // the only lock of the save-state mutex outside try_lock is the main-loop drain (SDL thread)
        val locks = Regex("""lock_guard[^;]*save_state_operation_mutex""").findAll(source).count()
        assertEquals(1, locks)
        val jni = source.substringAfter("RearTouchNative_setContact")
        assertFalse(jni.substringBefore("clearContacts").contains("save_state_operation_mutex"))
        assertTrue(core("android/jni/main_android.cpp").contains("apply_pending_touch_commands();"))
    }

    @Test
    fun saveStateBusyIsClaimedBeforeTheCoroutineIsDispatched() {
        val source = kotlin("ui/emulation/EmulationOverlay.kt")
        val body = source.substringAfter("fun performSaveStateAction(")
        val claim = body.indexOf("saveStateBusy = true")
        val launch = body.indexOf("overlayScope.launch")
        assertTrue(claim in 0 until launch)
    }

    @Test
    fun deadSystemTogglesAreGoneFromTheInGameMenu() {
        val menu = kotlin("ui/emulation/EmulationMenu.kt")
        assertFalse(menu.contains("onShowWelcome"))
        assertFalse(menu.contains("onWarnMissingFirmware"))
    }

    @Test
    fun menuChangesAreStoredAsOverridesOffTheUiThread() {
        val overlay = kotlin("ui/emulation/EmulationOverlay.kt")
        val persist = overlay.substringAfter("fun persistConfig(").substringBefore("fun applyRuntimeCoreSettings")
        assertTrue(persist.contains("profileWriter.submit"))
        assertFalse(persist.contains("savePreservingDriverOverride"))
        assertTrue(overlay.contains("Dispatchers.IO"))
        assertTrue(overlay.contains("repository.saveOverrides("))
    }

    @Test
    fun menuWindowStaysNonFocusableAndControllerKeysAreRoutedIn() {
        val window = kotlin("ui/emulation/EmulationMenuWindow.kt")
        assertTrue(window.contains("FLAG_NOT_FOCUSABLE"))
        assertTrue(window.contains("registerKeyDispatcher"))
        val emulator = kotlin("core/vita/Emulator.kt")
        assertTrue(emulator.contains("overlayMenuKeyHandler?.invoke(event)"))
    }

    @Test
    fun touchModeIsPersistedPerGame() {
        val overlay = kotlin("ui/emulation/EmulationOverlay.kt")
        assertTrue(overlay.contains("rearTouchRepository.touchMode(gameId)"))
        assertTrue(overlay.contains("rearTouchRepository.setTouchMode(gameId, mode)"))
    }
}
