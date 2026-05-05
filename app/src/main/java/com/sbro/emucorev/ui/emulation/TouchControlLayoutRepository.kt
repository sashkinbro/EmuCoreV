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
    val visible: Boolean = true
)

class TouchControlLayoutRepository(context: Context) {
    private val preferences = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    fun load(): List<TouchControlElement>? {
        val raw = preferences.getString(KEY_LAYOUT, null) ?: return null
        return runCatching {
            val array = JSONArray(raw)
            buildList {
                for (index in 0 until array.length()) {
                    val item = array.getJSONObject(index)
                    add(
                        TouchControlElement(
                            id = item.getString("id"),
                            x = item.getDouble("x").toFloat(),
                            y = item.getDouble("y").toFloat(),
                            width = item.getDouble("width").toFloat(),
                            height = item.getDouble("height").toFloat(),
                            visible = item.optBoolean("visible", true)
                        ).coerceToCanvas()
                    )
                }
            }
        }.getOrNull()
    }

    fun save(elements: List<TouchControlElement>) {
        val array = JSONArray()
        elements.forEach { element ->
            array.put(
                JSONObject()
                    .put("id", element.id)
                    .put("x", element.x)
                    .put("y", element.y)
                    .put("width", element.width)
                    .put("height", element.height)
                    .put("visible", element.visible)
            )
        }
        preferences.edit { putString(KEY_LAYOUT, array.toString()) }
    }

    fun reset() {
        preferences.edit {remove(KEY_LAYOUT)}
    }

    private fun TouchControlElement.coerceToCanvas(): TouchControlElement {
        val safeWidth = width.coerceIn(MIN_ELEMENT_SIZE, MAX_ELEMENT_SIZE)
        val safeHeight = height.coerceIn(MIN_ELEMENT_SIZE, MAX_ELEMENT_SIZE)
        return copy(
            width = safeWidth,
            height = safeHeight,
            x = x.coerceIn(0f, 1f - safeWidth),
            y = y.coerceIn(0f, 1f - safeHeight)
        )
    }

    private companion object {
        const val PREFS_NAME = "touch_control_layout"
        const val KEY_LAYOUT = "layout_v1"
        const val MIN_ELEMENT_SIZE = 0.035f
        const val MAX_ELEMENT_SIZE = 0.5f
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
    const val PS = "ps"
    const val START = "start"
    const val TOUCH = "touch"
}
