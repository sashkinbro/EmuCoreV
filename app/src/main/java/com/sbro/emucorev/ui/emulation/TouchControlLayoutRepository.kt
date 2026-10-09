package com.sbro.emucorev.ui.emulation

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import androidx.core.content.edit

data class TouchControlElement(
    val id: String,
    val x: Float,
    val y: Float,
    val width: Float,
    val height: Float,
    val visible: Boolean = true,
    val analogMode: TouchAnalogMode = TouchAnalogMode.Stick,
    val actionId: String = id,
    val secondaryActionId: String? = null,
    val opacity: Int = CONTROL_OPACITY_MAX
)

internal const val CONTROL_OPACITY_MIN = 10
internal const val CONTROL_OPACITY_MAX = 100
internal const val CONTROL_OPACITY_STEP = 10

internal fun TouchControlElement.normalized(): TouchControlElement {
    val safeWidth = width.takeIf(Float::isFinite)?.coerceIn(0.015f, 0.5f) ?: 0.08f
    val safeHeight = height.takeIf(Float::isFinite)?.coerceIn(0.015f, 0.5f) ?: 0.08f
    return copy(
        x = x.takeIf(Float::isFinite)?.coerceIn(0f, 1f - safeWidth) ?: 0.5f,
        y = y.takeIf(Float::isFinite)?.coerceIn(0f, 1f - safeHeight) ?: 0.5f,
        width = safeWidth, height = safeHeight,
        secondaryActionId = secondaryActionId?.takeIf { it in TouchControlIds.BUTTON_IDS && it != actionId },
        opacity = opacity.coerceIn(CONTROL_OPACITY_MIN, CONTROL_OPACITY_MAX)
    )
}

enum class TouchAnalogMode(val storageValue: String) {
    Stick("stick"),
    TouchArea("touch_area");

    companion object {
        fun fromStorage(value: String?): TouchAnalogMode {
            return entries.firstOrNull { it.storageValue == value } ?: Stick
        }
    }
}

class TouchControlLayoutRepository(context: Context) {
    private val preferences = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    fun load(): List<TouchControlElement>? {
        val raw = preferences.getString(KEY_LAYOUT, null)
        if (raw == null) {
            preferences.edit { putBoolean(KEY_SHOULDER_VISIBILITY_MIGRATED, true) }
            return null
        }
        val elements = runCatching {
            val array = JSONArray(raw)
            buildList {
                for (index in 0 until array.length()) {
                    val element = runCatching {
                        val item = array.getJSONObject(index)
                        TouchControlElement(
                            id = item.getString("id"),
                            x = item.getDouble("x").toFloat(),
                            y = item.getDouble("y").toFloat(),
                            width = item.getDouble("width").toFloat(),
                            height = item.getDouble("height").toFloat(),
                            visible = item.optBoolean("visible", true),
                            actionId = item.optString("actionId", item.getString("id")),
                            secondaryActionId = item.optString("secondaryActionId").takeIf { it.isNotBlank() },
                            analogMode = TouchAnalogMode.fromStorage(
                                if (item.has("analogMode")) item.optString("analogMode") else null
                            ),
                            opacity = item.optInt("opacity", CONTROL_OPACITY_MAX)
                        ).coerceToCanvas()
                    }.getOrNull()
                    if (element != null) add(element)
                }
            }
        }.getOrNull() ?: return null
        if (!preferences.getBoolean(KEY_SHOULDER_VISIBILITY_MIGRATED, false)) {
            val migrated = elements.map { element ->
                if (element.id == TouchControlIds.L2 || element.id == TouchControlIds.R2) {
                    element.copy(visible = false)
                } else {
                    element
                }
            }
            save(migrated)
            preferences.edit { putBoolean(KEY_SHOULDER_VISIBILITY_MIGRATED, true) }
            return migrated
        }
        return elements
    }

    fun save(elements: List<TouchControlElement>) {
        val array = JSONArray()
        elements.distinctBy { it.id }.map { it.normalized() }.forEach { element ->
            array.put(
                JSONObject()
                    .put("id", element.id)
                    .put("x", element.x)
                    .put("y", element.y)
                    .put("width", element.width)
                    .put("height", element.height)
                    .put("visible", element.visible)
                    .put("analogMode", element.analogMode.storageValue)
                    .put("actionId", element.actionId)
                    .put("secondaryActionId", element.secondaryActionId)
                    .put("opacity", element.opacity)
            )
        }
        preferences.edit { putString(KEY_LAYOUT, array.toString()) }
    }

    fun reset() {
        preferences.edit {remove(KEY_LAYOUT)}
    }

    private fun TouchControlElement.coerceToCanvas(): TouchControlElement {
        return normalized()
    }

    private companion object {
        const val PREFS_NAME = "touch_control_layout"
        const val KEY_LAYOUT = "layout_v1"
        const val KEY_SHOULDER_VISIBILITY_MIGRATED = "shoulder_visibility_migrated_v1"
    }
}

object TouchControlIds {
    const val L2 = "l2"
    const val L1 = "l1"
    const val R2 = "r2"
    const val R1 = "r1"
    const val DPAD_UP = "dpad_up"
    const val DPAD_DOWN = "dpad_down"
    const val DPAD_LEFT = "dpad_left"
    const val DPAD_RIGHT = "dpad_right"
    const val LEFT_STICK = "left_stick"
    const val RIGHT_STICK = "right_stick"
    const val TRIANGLE = "triangle"
    const val CROSS = "cross"
    const val SQUARE = "square"
    const val CIRCLE = "circle"
    const val SELECT = "select"
    const val START = "start"
    const val TOUCH = "touch"

    val BUTTON_IDS = setOf(L2, L1, R2, R1, DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT,
        TRIANGLE, CROSS, SQUARE, CIRCLE, SELECT, START)
}
