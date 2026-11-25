package com.sbro.emucorev.core.input

import android.content.Context
import androidx.core.content.edit
import org.json.JSONArray
import org.json.JSONObject
import java.util.Locale

/** Matches the existing touch-layout profile: a global base, optional whole-game override. */
class RearTouchProfileRepository(context: Context) {
    private val preferences = context.applicationContext.getSharedPreferences("rear_touch_profiles", Context.MODE_PRIVATE)
    fun global(): RearTouchProfile = decode(preferences.getString("global", null)) ?: RearTouchProfile()
    fun override(titleId: String): RearTouchProfile? = key(titleId)?.let { decode(preferences.getString(it, null)) }
    fun effective(titleId: String): RearTouchProfile = override(titleId) ?: global()
    fun saveGlobal(profile: RearTouchProfile) { preferences.edit { putString("global", encode(profile)) } }
    fun saveGame(titleId: String, profile: RearTouchProfile) { key(titleId)?.let { preferences.edit { putString(it, encode(profile)) } } }
    fun inherit(titleId: String) { key(titleId)?.let { preferences.edit { remove(it) } } }
    /** Front/rear/both screen-touch mode (0..2) remembered per title; 0 when never changed. */
    fun touchMode(titleId: String): Int = touchModeKey(titleId)?.let { preferences.getInt(it, 0).coerceIn(0, 2) } ?: 0
    fun setTouchMode(titleId: String, mode: Int) {
        val key = touchModeKey(titleId) ?: return
        preferences.edit { if (mode in 1..2) putInt(key, mode) else remove(key) }
    }
    private fun touchModeKey(titleId: String): String? = key(titleId)?.let { "touch_mode_${it.removePrefix("game_")}" }

    /** Every stored profile (global + per game) for the settings/Drive backup. */
    fun exportAll(): JSONObject = JSONObject().also { out ->
        preferences.all.forEach { (name, value) ->
            if (value is String && decode(value) != null) out.put(name, value)
            else if (value is Int && name.startsWith("touch_mode_") && value in 1..2) out.put(name, value)
        }
    }

    /** Restores profiles from [exportAll]; invalid entries and unknown keys are ignored. */
    fun restoreAll(root: JSONObject) {
        preferences.edit {
            root.keys().forEach { name ->
                if (name.matches(Regex("touch_mode_[A-Z0-9]{9}"))) { root.optInt(name, 0).takeIf { it in 1..2 }?.let { putInt(name, it) }; return@forEach }
                val raw = root.optString(name)
                if ((name == "global" || name.matches(Regex("game_[A-Z0-9]{9}"))) && decode(raw) != null) putString(name, raw)
            }
        }
    }
    private fun key(titleId: String): String? = titleId.trim().uppercase(Locale.ROOT)
        .takeIf { it.matches(Regex("[A-Z0-9]{9}")) }?.let { "game_$it" }

    companion object {
        fun encode(profile: RearTouchProfile): String = JSONObject().put("version", 1).put("zone", profile.normalized().zone.let { z -> JSONObject()
            .put("left", z.left.toDouble()).put("top", z.top.toDouble()).put("right", z.right.toDouble()).put("bottom", z.bottom.toDouble()) }).put("points",
            JSONArray().apply { profile.normalized().points.forEach { p -> put(JSONObject()
                .put("slot", p.slot).put("x", p.x.toDouble()).put("y", p.y.toDouble())
                .put("screen", p.screenEnabled).put("screenX", p.screenX.toDouble()).put("screenY", p.screenY.toDouble())
                .put("position", p.positionMode.name).put("trigger", p.trigger.name).put("bindings", JSONArray().apply { p.bindings.forEach { b -> put(JSONObject()
                    .put("kind", b.kind.name).put("code", b.code).put("direction", b.direction).put("device", b.device)) } })) }
            }).toString()

        fun decode(raw: String?): RearTouchProfile? = raw?.let {
            runCatching {
                val root = JSONObject(it)
                require(root.optInt("version") == 1)
                val array = root.getJSONArray("points")
                RearTouchProfile((0 until minOf(array.length(), 4)).map { index ->
                    val p = array.getJSONObject(index)
                    val bindings = p.optJSONArray("bindings") ?: JSONArray()
                    RearTouchPoint(slot = p.getInt("slot"), x = p.optDouble("x", .5).toFloat(), y = p.optDouble("y", .5).toFloat(),
                        screenEnabled = p.optBoolean("screen"), screenX = p.optDouble("screenX", .5).toFloat(), screenY = p.optDouble("screenY", .5).toFloat(),
                        positionMode = RearPositionMode.entries.firstOrNull { mode -> mode.name == p.optString("position") } ?: RearPositionMode.FIXED,
                        trigger = RearTriggerMode.entries.firstOrNull { t -> t.name == p.optString("trigger") } ?: RearTriggerMode.HOLD,
                        bindings = (0 until minOf(bindings.length(), 8)).mapNotNull { i ->
                            val b = bindings.optJSONObject(i) ?: return@mapNotNull null
                            val kind = RearBindingKind.entries.firstOrNull { kind -> kind.name == b.optString("kind") } ?: return@mapNotNull null
                            RearTouchBinding(kind, b.optInt("code", -1), if (b.optInt("direction", 1) < 0) -1 else 1, b.optString("device"))
                        })
                }, root.optJSONObject("zone")?.let { z -> RearZone(z.optDouble("left", 0.0).toFloat(), z.optDouble("top", 0.0).toFloat(),
                    z.optDouble("right", 1.0).toFloat(), z.optDouble("bottom", 1.0).toFloat()) } ?: RearZone.FULL).normalized()
            }.getOrNull()
        }
    }
}
