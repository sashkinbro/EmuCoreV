package com.sbro.emucorev.data

import android.content.Context
import java.io.Closeable
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

data class SaveStateAutomationSettings(
    val autoSaveOnExit: Boolean = false,
    val autoLoadOnStart: Boolean = false
)

/** Frontend options are independent for every title, including homebrew. */
class SaveStateAutomationPreferences(context: Context, titleId: String) : Closeable {
    private val title = titleId.trim()
    private val prefix = "game:${title.length}:$title:"
    private val prefs = context.applicationContext.getSharedPreferences("save_state_automation", Context.MODE_PRIVATE)
    val current: SaveStateAutomationSettings get() = if (title.isEmpty()) SaveStateAutomationSettings() else
        SaveStateAutomationSettings(prefs.all[prefix + "save"] == true, prefs.all[prefix + "load"] == true)
    private val mutableSettings = MutableStateFlow(current)
    val settings = mutableSettings.asStateFlow()
    private val listener = android.content.SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
        if (key == null || key.startsWith(prefix)) mutableSettings.value = current
    }

    init { prefs.registerOnSharedPreferenceChangeListener(listener); mutableSettings.value = current }

    fun setAutoSaveOnExit(enabled: Boolean) { if (title.isNotEmpty()) prefs.edit().putBoolean(prefix + "save", enabled).apply() }
    fun setAutoLoadOnStart(enabled: Boolean) { if (title.isNotEmpty()) prefs.edit().putBoolean(prefix + "load", enabled).apply() }
    fun reset() { prefs.edit().remove(prefix + "save").remove(prefix + "load").apply() }
    override fun close() { prefs.unregisterOnSharedPreferenceChangeListener(listener) }
}
