package com.sbro.emucorev.core

import android.content.Context
import com.sbro.emucorev.data.KeyboardMouseSettings
import java.io.File
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class VitaGameSettingsRepositoryKeyboardMouseTest {
    private lateinit var context: Context
    private lateinit var configDirectory: File
    private val titleId = "PCSE00000"

    @Before
    fun setUp() {
        context = RuntimeEnvironment.getApplication()
        configDirectory = File(EmulatorStorage.runtimeRoot(context), "config")
        configDirectory.deleteRecursively()
        context.getSharedPreferences("emucorev_ui_config", Context.MODE_PRIVATE).edit().clear().commit()
    }

    @After
    fun tearDown() {
        configDirectory.deleteRecursively()
        context.getSharedPreferences("emucorev_ui_config", Context.MODE_PRIVATE).edit().clear().commit()
    }

    @Test
    fun perGameOverrideRoundTripsInsideTheGameConfig() {
        val repository = VitaGameSettingsRepository(context)
        val settings = KeyboardMouseSettings(
            keyboardTargetPad = 1,
            mouseTargetPad = 0,
            keyboardBindingsByPad = mapOf(0 to mapOf("cross" to 42)),
            mouseBindingsByPad = mapOf(1 to mapOf("circle" to 1)),
            mouseAimMode = KeyboardMouseBindings.MOUSE_AIM_RIGHT_STICK,
            mouseSensitivity = 175
        )

        repository.saveProfile(titleId, VitaCoreConfig(), null, settings)

        val profile = repository.loadProfile(titleId)
        assertEquals(settings, profile.keyboardMouse)
        val file = repository.customConfigFile(titleId)
        assertTrue(file.isFile)
        val xml = file.readText()
        assertTrue(xml.contains("<keyboard-mouse>"))
        assertTrue(xml.contains("\"mouseSensitivity\":175"))
    }

    @Test
    fun regularConfigSavesPreserveTheKeyboardMouseOverride() {
        val repository = VitaGameSettingsRepository(context)
        val settings = KeyboardMouseSettings(mouseAimMode = KeyboardMouseBindings.MOUSE_AIM_LEFT_STICK)
        repository.saveProfile(titleId, VitaCoreConfig(), null, settings)

        repository.saveProfile(titleId, VitaCoreConfig(pstvMode = true), null)

        val profile = repository.loadProfile(titleId)
        assertTrue(profile.config.pstvMode)
        assertEquals(settings, profile.keyboardMouse)
    }

    @Test
    fun clearingTheOverrideKeepsOtherPerGameSettings() {
        val repository = VitaGameSettingsRepository(context)
        val settings = KeyboardMouseSettings(mouseEnabled = false)
        repository.saveProfile(titleId, VitaCoreConfig(pstvMode = true), null, settings)

        repository.updateKeyboardMouse(titleId, null)

        val profile = repository.loadProfile(titleId)
        assertNull(profile.keyboardMouse)
        assertTrue(profile.config.pstvMode)
        assertTrue(repository.customConfigFile(titleId).isFile)
    }

    @Test
    fun clearingAnOverrideOnANonCustomGameDoesNotCreateAProfile() {
        val repository = VitaGameSettingsRepository(context)

        repository.updateKeyboardMouse(titleId, null)

        assertNull(repository.loadProfile(titleId).keyboardMouse)
        assertTrue(!repository.customConfigFile(titleId).exists())
    }

    @Test
    fun resettingTheGameProfileRemovesTheKeyboardMouseOverride() {
        val repository = VitaGameSettingsRepository(context)
        repository.saveProfile(
            titleId,
            VitaCoreConfig(),
            null,
            KeyboardMouseSettings(mouseEnabled = false)
        )
        assertNotNull(repository.loadProfile(titleId).keyboardMouse)

        repository.reset(titleId)

        assertNull(repository.loadProfile(titleId).keyboardMouse)
    }
}
