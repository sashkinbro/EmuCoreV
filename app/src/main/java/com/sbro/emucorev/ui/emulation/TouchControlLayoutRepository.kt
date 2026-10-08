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
    val secondaryActionId: String? = null
)

internal fun TouchControlElement.normalized(): TouchControlElement {
    val safeWidth = width.takeIf(Float::isFinite)?.coerceIn(0.015f, 0.5f) ?: 0.08f
    val safeHeight = height.takeIf(Float::isFinite)?.coerceIn(0.015f, 0.5f) ?: 0.08f
    return copy(
        x = x.takeIf(Float::isFinite)?.coerceIn(0f, 1f - safeWidth) ?: 0.5f,
        y = y.takeIf(Float::isFinite)?.coerceIn(0f, 1f - safeHeight) ?: 0.5f,
        width = safeWidth, height = safeHeight,
        secondaryActionId = secondaryActionId?.takeIf { it in TouchControlIds.BUTTON_IDS && it != actionId }
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
        val raw = preferences.getString(KEY_LAYOUT, null) ?: return null
        return runCatching {
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
                            )
                        ).coerceToCanvas()
                    }.getOrNull()
                    if (element != null) add(element)
                }
            }
        }.getOrNull()
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
