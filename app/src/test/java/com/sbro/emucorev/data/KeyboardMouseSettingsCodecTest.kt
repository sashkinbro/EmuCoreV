package com.sbro.emucorev.data

import android.content.Context
import com.sbro.emucorev.core.KeyboardMouseBindings
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class KeyboardMouseSettingsCodecTest {
    @Test
    fun roundTripPreservesBindingsAndMouseOptions() {
        val settings = KeyboardMouseSettings(
            keyboardEnabled = false,
            mouseEnabled = true,
            keyboardTargetPad = 1,
            mouseTargetPad = 0,
            keyboardBindingsByPad = mapOf(
                0 to mapOf("cross" to 42, "l2" to 100),
                1 to mapOf("quick_save" to 12)
            ),
            mouseBindingsByPad = mapOf(1 to mapOf("circle" to KeyboardMouseBindings.MOUSE_BUTTON_RIGHT)),
            mouseAimMode = KeyboardMouseBindings.MOUSE_AIM_LEFT_STICK,
            mouseSensitivity = 180,
            mouseVerticalSensitivity = 220,
            mouseSmoothing = 35,
            mouseAimDeadzone = 12,
            mouseInvertX = true,
            mouseInvertY = true,
            mouseCaptureCursor = false,
            mouseLeftHanded = true,
            mouseWheelMode = KeyboardMouseBindings.MOUSE_WHEEL_SHOULDERS,
            mouseWheelPulseMs = 140
        )

        val decoded = KeyboardMouseSettingsCodec.decode(
            JSONObject(KeyboardMouseSettingsCodec.encode(settings).toString())
        )

        assertEquals(settings, decoded)
    }

    @Test
    fun missingOrMalformedValuesFallBackToSafeDefaults() {
        assertNull(KeyboardMouseSettingsCodec.decode(null))

        val decoded = KeyboardMouseSettingsCodec.decode(
            JSONObject(
                """
                {
                  "keyboardTargetPad": 9,
                  "mouseTargetPad": -4,
                  "mouseAimMode": 99,
                  "mouseSensitivity": 999,
                  "mouseVerticalSensitivity": 1,
                  "mouseSmoothing": 900,
                  "mouseAimDeadzone": 999,
                  "mouseWheelMode": 99,
                  "mouseWheelPulseMs": 9999
                }
                """.trimIndent()
            )
        )!!

        assertEquals(1, decoded.keyboardTargetPad)
        assertEquals(0, decoded.mouseTargetPad)
        assertEquals(KeyboardMouseBindings.MOUSE_AIM_LEFT_STICK, decoded.mouseAimMode)
        assertEquals(KeyboardMouseBindings.MOUSE_SENSITIVITY_MAX, decoded.mouseSensitivity)
        assertEquals(
            KeyboardMouseBindings.MOUSE_VERTICAL_SENSITIVITY_MIN,
            decoded.mouseVerticalSensitivity
        )
        assertEquals(AppPreferences.MOUSE_SMOOTHING_MAX, decoded.mouseSmoothing)
        assertEquals(KeyboardMouseBindings.MOUSE_DEADZONE_MAX, decoded.mouseAimDeadzone)
        assertEquals(KeyboardMouseBindings.MOUSE_WHEEL_SHOULDERS, decoded.mouseWheelMode)
        assertEquals(
            KeyboardMouseBindings.MOUSE_WHEEL_PULSE_MAX_MS,
            decoded.mouseWheelPulseMs
        )
    }
}

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class AppPreferencesKeyboardMouseTest {
    private lateinit var context: Context

    @Before
    fun clearKeyboardMouse() {
        context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("emucorev_prefs", Context.MODE_PRIVATE)
            .edit().remove("keyboard_mouse_settings").commit()
    }

    @Test
    fun settingsSurvivePreferenceRecreation() {
        assertEquals(KeyboardMouseSettings(), AppPreferences(context).keyboardMouseSettings)

        val settings = KeyboardMouseSettings(
            keyboardTargetPad = 1,
            mouseBindingsByPad = mapOf(0 to mapOf("cross" to 1)),
            mouseAimMode = KeyboardMouseBindings.MOUSE_AIM_RIGHT_STICK,
            mouseWheelMode = KeyboardMouseBindings.MOUSE_WHEEL_DPAD
        )
        AppPreferences(context).keyboardMouseSettings = settings

        assertEquals(settings, AppPreferences(context).keyboardMouseSettings)
    }

    @Test
    fun flowTracksSettingsFromOtherPreferenceInstances() = runBlocking {
        val first = KeyboardMouseSettings(keyboardTargetPad = 1)
        val second = first.copy(mouseTargetPad = 1)
        val values = mutableListOf<KeyboardMouseSettings>()
        val collector = launch(Dispatchers.Unconfined) {
            AppPreferences(context).keyboardMouseSettingsFlow.take(3).toList(values)
        }
        AppPreferences(context).keyboardMouseSettings = first
        AppPreferences(context).keyboardMouseSettings = second
        withTimeout(1_000) { collector.join() }
        assertEquals(listOf(KeyboardMouseSettings(), first, second), values)
    }
}
