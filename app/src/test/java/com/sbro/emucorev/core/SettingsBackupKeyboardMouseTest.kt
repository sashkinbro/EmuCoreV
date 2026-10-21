package com.sbro.emucorev.core

import android.content.Context
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.CustomizationPreferences
import com.sbro.emucorev.data.KeyboardMouseSettings
import com.sbro.emucorev.core.KeyboardMouseBindings
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class SettingsBackupKeyboardMouseTest {
    private lateinit var context: Context
    private lateinit var customization: CustomizationPreferences

    @Before
    fun setUp() {
        context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("emucorev_prefs", Context.MODE_PRIVATE)
            .edit().remove("keyboard_mouse_settings").commit()
        customization = CustomizationPreferences(context)
    }

    @After
    fun tearDown() {
        customization.close()
        context.getSharedPreferences("emucorev_prefs", Context.MODE_PRIVATE)
            .edit().remove("keyboard_mouse_settings").commit()
    }

    private fun backup() = SettingsBackupRepository(
        context,
        AppPreferences(context),
        VitaCoreConfigRepository(context),
        customization
    )

    @Test
    fun keyboardMouseSettingsRoundTripThroughTheSettingsBackup() {
        val settings = KeyboardMouseSettings(
            keyboardTargetPad = 1,
            mouseTargetPad = 1,
            keyboardBindingsByPad = mapOf(1 to mapOf("cross" to 42)),
            mouseAimMode = KeyboardMouseBindings.MOUSE_AIM_RIGHT_STICK,
            mouseSensitivity = 175,
            mouseWheelMode = KeyboardMouseBindings.MOUSE_WHEEL_TRIGGERS
        )
        AppPreferences(context).keyboardMouseSettings = settings
        val exported = backup().exportJson()

        AppPreferences(context).keyboardMouseSettings = KeyboardMouseSettings()
        backup().restoreJson(exported, applyCore = false, applyCustomization = false)

        assertEquals(settings, AppPreferences(context).keyboardMouseSettings)
    }

    @Test
    fun olderBackupWithoutKeyboardMouseKeepsTheCurrentSettings() {
        val current = KeyboardMouseSettings(mouseEnabled = false)
        AppPreferences(context).keyboardMouseSettings = current

        backup().restoreJson(
            JSONObject().put("format", 1),
            applyCore = false,
            applyCustomization = false
        )

        assertEquals(current, AppPreferences(context).keyboardMouseSettings)
    }
}
