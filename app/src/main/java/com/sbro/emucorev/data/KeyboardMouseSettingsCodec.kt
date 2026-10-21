package com.sbro.emucorev.data

import org.json.JSONObject

internal object KeyboardMouseSettingsCodec {
    fun encode(settings: KeyboardMouseSettings): JSONObject = JSONObject().apply {
        put("keyboardEnabled", settings.keyboardEnabled)
        put("mouseEnabled", settings.mouseEnabled)
        put("keyboardTargetPad", settings.keyboardTargetPad)
        put("mouseTargetPad", settings.mouseTargetPad)
        put("keyboardBindingsByPad", encodeBindings(settings.keyboardBindingsByPad))
        put("mouseBindingsByPad", encodeBindings(settings.mouseBindingsByPad))
        put("mouseAimMode", settings.mouseAimMode)
        put("mouseSensitivity", settings.mouseSensitivity)
        put("mouseVerticalSensitivity", settings.mouseVerticalSensitivity)
        put("mouseSmoothing", settings.mouseSmoothing)
        put("mouseAimDeadzone", settings.mouseAimDeadzone)
        put("mouseInvertX", settings.mouseInvertX)
        put("mouseInvertY", settings.mouseInvertY)
        put("mouseCaptureCursor", settings.mouseCaptureCursor)
        put("mouseLeftHanded", settings.mouseLeftHanded)
        put("mouseWheelMode", settings.mouseWheelMode)
        put("mouseWheelPulseMs", settings.mouseWheelPulseMs)
    }

    fun decode(json: JSONObject?): KeyboardMouseSettings? {
        if (json == null) return null
        return KeyboardMouseSettings(
            keyboardEnabled = json.optBoolean("keyboardEnabled", true),
            mouseEnabled = json.optBoolean("mouseEnabled", true),
            keyboardTargetPad = json.optInt("keyboardTargetPad", 0).coerceIn(0, 1),
            mouseTargetPad = json.optInt("mouseTargetPad", 0).coerceIn(0, 1),
            keyboardBindingsByPad = decodeBindings(json.optJSONObject("keyboardBindingsByPad")),
            mouseBindingsByPad = decodeBindings(json.optJSONObject("mouseBindingsByPad")),
            mouseAimMode = json.optInt("mouseAimMode", AppPreferences.MOUSE_AIM_OFF)
                .coerceIn(AppPreferences.MOUSE_AIM_OFF, AppPreferences.MOUSE_AIM_LEFT_STICK),
            mouseSensitivity = json.optInt(
                "mouseSensitivity",
                AppPreferences.DEFAULT_MOUSE_SENSITIVITY
            ).coerceIn(AppPreferences.MOUSE_SENSITIVITY_MIN, AppPreferences.MOUSE_SENSITIVITY_MAX),
            mouseVerticalSensitivity = json.optInt(
                "mouseVerticalSensitivity",
                AppPreferences.DEFAULT_MOUSE_VERTICAL_SENSITIVITY
            ).coerceIn(
                AppPreferences.MOUSE_VERTICAL_SENSITIVITY_MIN,
                AppPreferences.MOUSE_VERTICAL_SENSITIVITY_MAX
            ),
            mouseSmoothing = json.optInt(
                "mouseSmoothing",
                AppPreferences.DEFAULT_MOUSE_SMOOTHING
            ).coerceIn(AppPreferences.MOUSE_SMOOTHING_MIN, AppPreferences.MOUSE_SMOOTHING_MAX),
            mouseAimDeadzone = json.optInt(
                "mouseAimDeadzone",
                AppPreferences.DEFAULT_MOUSE_DEADZONE
            ).coerceIn(AppPreferences.MOUSE_DEADZONE_MIN, AppPreferences.MOUSE_DEADZONE_MAX),
            mouseInvertX = json.optBoolean("mouseInvertX", false),
            mouseInvertY = json.optBoolean("mouseInvertY", false),
            mouseCaptureCursor = json.optBoolean("mouseCaptureCursor", true),
            mouseLeftHanded = json.optBoolean("mouseLeftHanded", false),
            mouseWheelMode = json.optInt("mouseWheelMode", AppPreferences.MOUSE_WHEEL_OFF)
                .coerceIn(AppPreferences.MOUSE_WHEEL_OFF, AppPreferences.MOUSE_WHEEL_SHOULDERS),
            mouseWheelPulseMs = json.optInt(
                "mouseWheelPulseMs",
                AppPreferences.DEFAULT_MOUSE_WHEEL_PULSE_MS
            ).coerceIn(
                AppPreferences.MOUSE_WHEEL_PULSE_MIN_MS,
                AppPreferences.MOUSE_WHEEL_PULSE_MAX_MS
            )
        )
    }

    private fun encodeBindings(bindingsByPad: Map<Int, Map<String, Int>>): JSONObject {
        return JSONObject().apply {
            bindingsByPad.toSortedMap().forEach { (padIndex, bindings) ->
                if (bindings.isEmpty()) return@forEach
                put(
                    padIndex.toString(),
                    JSONObject().apply {
                        bindings.toSortedMap().forEach { (actionId, code) -> put(actionId, code) }
                    }
                )
            }
        }
    }

    private fun decodeBindings(json: JSONObject?): Map<Int, Map<String, Int>> {
        if (json == null) return emptyMap()
        return buildMap {
            json.keys().forEach { padKey ->
                val padIndex = padKey.toIntOrNull()?.coerceIn(0, 1) ?: return@forEach
                val padObject = json.optJSONObject(padKey) ?: return@forEach
                val bindings = buildMap {
                    padObject.keys().forEach { actionId ->
                        val code = padObject.optInt(actionId, Int.MIN_VALUE)
                        if (code != Int.MIN_VALUE) put(actionId, code)
                    }
                }
                if (bindings.isNotEmpty()) put(padIndex, bindings)
            }
        }
    }
}
