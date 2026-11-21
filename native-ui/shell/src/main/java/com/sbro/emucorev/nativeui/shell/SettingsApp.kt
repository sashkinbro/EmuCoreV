package com.sbro.emucorev.nativeui.shell

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.os.Build

/** Options of the native shell mode only (never the application's own settings). */
class ShellSettings(context: Context) {
    private val prefs = context.getSharedPreferences("native_shell_settings", Context.MODE_PRIVATE)

    var waveTheme: String
        get() = prefs.getString("wave_theme", DEFAULT_THEME) ?: DEFAULT_THEME
        set(value) = prefs.edit().putString("wave_theme", value).apply()
    var systemSounds: Boolean
        get() = prefs.getBoolean("system_sounds", true)
        set(value) = prefs.edit().putBoolean("system_sounds", value).apply()
    /** 0 automatic (device), 1 12-hour, 2 24-hour. */
    var clock: Int
        get() = prefs.getInt("clock", 0)
        set(value) = prefs.edit().putInt("clock", value).apply()
    /** 0 full screen (adaptive), 1 original 960x544 aspect. */
    var displayArea: Int
        get() = prefs.getInt("display_area", 0)
        set(value) = prefs.edit().putInt("display_area", value).apply()

    companion object { const val DEFAULT_THEME = "effects_default_0512_01" }
}

/** What Settings needs from the shell besides drawing pages. */
interface SettingsActions {
    val settings: ShellSettings
    val waveThemes: List<String>
    val applicationVersion: String
    fun applyWaveTheme(name: String)
    fun applyDisplayArea(mode: Int)
    fun applyClock()
    fun exitMode()
}

/**
 * Settings (NPXS10015) for this mode, laid out from system_settings_main
 * templates: 960x64 header from y 32 (32 px bold title, 960x2 separator
 * 69cb3790), 960x448 list from y 96, 840x80 buttons (bg 29b91f26, glow
 * d0a272a1), 60x60 icon, 28 px bold label, 20 px bold value, 46x46 arrow
 * 1845d10d or check box 23f741a2.
 */
class SettingsApp(private val actions: SettingsActions) : ShellApp(TITLE_ID) {
    private var easterTaps = 0

    private data class Item(val label: String, val icon: Int = 0, val value: String? = null,
        val check: Boolean? = null, val arrow: Boolean = false, val selectable: Boolean = true)

    override fun open(host: ShellAppHost) {
        easterTaps = 0
        host.show(page(host, ROOT), ShellTransition.OPEN)
    }

    override fun onRow(host: ShellAppHost, key: Int, row: Int) {
        val s = actions.settings
        when (key) {
            ROOT -> when (row) {
                0 -> push(host, THEME)
                1 -> push(host, SOUND_DISPLAY)
                2 -> push(host, DATE_TIME)
                3 -> push(host, SYSTEM)
                4 -> actions.exitMode()
            }
            THEME -> actions.waveThemes.getOrNull(row)?.let {
                s.waveTheme = it
                actions.applyWaveTheme(it)
                host.show(page(host, THEME), ShellTransition.REPLACE)
            }
            SOUND_DISPLAY -> {
                when (row) {
                    0 -> s.systemSounds = !s.systemSounds
                    1 -> { s.displayArea = 0; actions.applyDisplayArea(0) }
                    2 -> { s.displayArea = 1; actions.applyDisplayArea(1) }
                }
                host.show(page(host, SOUND_DISPLAY), ShellTransition.REPLACE)
            }
            DATE_TIME -> if (row in 0..2) {
                s.clock = row
                actions.applyClock()
                host.show(page(host, DATE_TIME), ShellTransition.REPLACE)
            }
            SYSTEM -> if (row == 0) push(host, SYSTEM_INFO)
            SYSTEM_INFO -> if (row == 0 && ++easterTaps == 7) host.show(page(host, SYSTEM_INFO), ShellTransition.REPLACE)
        }
    }

    override fun onBack(host: ShellAppHost, key: Int) {
        when (key) {
            ROOT -> host.close()
            SYSTEM_INFO -> host.show(page(host, SYSTEM), ShellTransition.POP)
            else -> host.show(page(host, ROOT), ShellTransition.POP)
        }
    }

    private fun push(host: ShellAppHost, key: Int) = host.show(page(host, key), ShellTransition.PUSH)

    private fun page(host: ShellAppHost, key: Int): ShellPage {
        val s = actions.settings
        val (title, items) = when (key) {
            ROOT -> host.string(R.string.native_ui_settings) to listOf(
                Item(host.string(R.string.native_ui_set_theme_background), ICON_THEME, arrow = true),
                Item(host.string(R.string.native_ui_set_sound_display), ICON_SOUND, arrow = true),
                Item(host.string(R.string.native_ui_set_date_time), ICON_CLOCK, arrow = true),
                Item(host.string(R.string.native_ui_set_system), ICON_SYSTEM, arrow = true),
                Item(host.string(R.string.native_ui_set_exit_mode), ICON_EXIT),
            )
            THEME -> host.string(R.string.native_ui_set_theme) to actions.waveThemes.mapIndexed { i, name ->
                Item(if (i == 0) host.string(R.string.native_ui_set_default)
                    else "${host.string(R.string.native_ui_set_theme)} ${i + 1}",
                    check = name == s.waveTheme)
            }
            SOUND_DISPLAY -> host.string(R.string.native_ui_set_sound_display) to listOf(
                Item(host.string(R.string.native_ui_set_system_sounds), check = s.systemSounds),
                Item("${host.string(R.string.native_ui_set_display_area)}: ${host.string(R.string.native_ui_set_full)}",
                    check = s.displayArea == 0),
                Item("${host.string(R.string.native_ui_set_display_area)}: ${host.string(R.string.native_ui_set_original)}",
                    check = s.displayArea == 1),
            )
            DATE_TIME -> host.string(R.string.native_ui_set_time_format) to listOf(
                Item(host.string(R.string.native_ui_set_automatic), check = s.clock == 0),
                Item(host.string(R.string.native_ui_set_clock12), check = s.clock == 1),
                Item(host.string(R.string.native_ui_set_clock24), check = s.clock == 2),
            )
            SYSTEM -> host.string(R.string.native_ui_set_system) to listOf(
                Item(host.string(R.string.native_ui_set_system_information), ICON_INFO, arrow = true),
            )
            else -> host.string(R.string.native_ui_set_system_information) to buildList {
                add(Item(host.string(R.string.native_ui_set_system_software),
                    value = host.string(R.string.native_ui_set_edition)))
                add(Item(host.string(R.string.native_ui_set_application), value = actions.applicationVersion,
                    selectable = false))
                add(Item(host.string(R.string.native_ui_set_device), value = Build.MODEL, selectable = false))
                if (easterTaps >= 7) add(Item(host.string(R.string.native_ui_set_easter_egg), selectable = false))
            }
        }
        val text = PafText(host.resources)
        val rows = items.map { row(host, text, it) }
        return ShellPage(TITLE_ID, key, BACKGROUND, header(host, text, title),
            floatArrayOf(0f, 32f, 960f, 64f), floatArrayOf(0f, 96f, 960f, 448f), rows)
    }

    private fun header(host: ShellAppHost, text: PafText, title: String): Bitmap {
        val bitmap = Bitmap.createBitmap(960, 64, Bitmap.Config.ARGB_8888)
        val c = Canvas(bitmap)
        text.line(c, title, 480f, 32f, 760f, text.paint(32f, bold = true), Paint.Align.CENTER)
        c.drawTexture(host.texture(SEPARATOR), RectF(0f, 62f, 960f, 64f))
        return bitmap
    }

    private fun row(host: ShellAppHost, text: PafText, item: Item): ShellRow {
        val bitmap = Bitmap.createBitmap(840, 80, Bitmap.Config.ARGB_8888)
        val c = Canvas(bitmap)
        var labelX = 24f
        if (item.icon != 0) {
            c.drawTexture(host.texture(item.icon), RectF(20f, 10f, 80f, 70f))
            labelX = 96f
        }
        val trailing = if (item.arrow || item.check != null) 70f else 24f
        val valueWidth = if (item.value != null) 300f else 0f
        val label = text.paint(28f, bold = true)
        if (!item.selectable && item.value == null) {
            // Free text row (easter egg): wrap over the full width.
            text.line(c, item.label, labelX, 40f, 840f - labelX - 24f, text.paint(20f, bold = true))
        } else {
            text.line(c, item.label, labelX, 40f, 840f - labelX - trailing - valueWidth - 16f, label)
        }
        item.value?.let {
            text.line(c, it, 840f - trailing, 40f, valueWidth, text.paint(20f, bold = true), Paint.Align.RIGHT)
        }
        if (item.arrow) c.drawTexture(host.texture(ARROW), RectF(774f, 17f, 820f, 63f))
        item.check?.let { checked ->
            // Check box texture holds two 64x64 frames (unchecked, checked).
            host.texture(CHECK_BOX)?.let { box ->
                val frame = if (checked) 1 else 0
                c.drawBitmap(box, Rect(0, frame * 64, 64, frame * 64 + 64), RectF(774f, 17f, 820f, 63f),
                    Paint(Paint.FILTER_BITMAP_FLAG))
            }
        }
        return ShellRow(bitmap, if (item.selectable) ROW_PLATE else 0, if (item.selectable) ROW_GLOW else 0,
            60f, 840f, 80f, item.selectable)
    }

    private fun themeLabel(name: String) = name.removePrefix("effects_")
        .split('_').filter { part -> part.none(Char::isDigit) || part.length <= 2 }
        .joinToString(" ") { part -> part.replaceFirstChar(Char::uppercaseChar) }
        .ifBlank { name }

    companion object {
        const val TITLE_ID = "NPXS10015"
        private const val ROOT = 0
        private const val THEME = 1
        private const val SOUND_DISPLAY = 2
        private const val DATE_TIME = 3
        private const val SYSTEM = 4
        private const val SYSTEM_INFO = 5

        const val BACKGROUND = 0xa67cb5df.toInt()
        const val ROW_PLATE = 0x29b91f26
        const val ROW_GLOW = 0xd0a272a1.toInt()
        const val SEPARATOR = 0x69cb3790
        const val ARROW = 0x1845d10d
        const val CHECK_BOX = 0x23f741a2
        const val ICON_THEME = 0xedeedd53.toInt()
        const val ICON_SOUND = 0xfd6e99ec.toInt()
        const val ICON_CLOCK = 0xcb2a108c.toInt()
        const val ICON_SYSTEM = 0x1b9bdf87
        const val ICON_INFO = 0x1b9bdf87
        const val ICON_EXIT = 0xf7cd75a9.toInt()

        val TEXTURES = mapOf(
            BACKGROUND to "images/system_settings_main/a67cb5df.png",
            ROW_PLATE to "images/system_settings_main/29b91f26.png",
            ROW_GLOW to "images/system_settings_main/d0a272a1.png",
            SEPARATOR to "images/system_settings_main/69cb3790.png",
            ARROW to "images/paf-common-resource/1845d10d.png",
            CHECK_BOX to "images/paf-common-resource/23f741a2.png",
            ICON_THEME to "images/system_settings_plugin/edeedd53.png",
            ICON_SOUND to "images/system_settings_plugin/fd6e99ec.png",
            ICON_CLOCK to "images/system_settings_plugin/cb2a108c.png",
            ICON_SYSTEM to "images/system_settings_plugin/1b9bdf87.png",
            ICON_EXIT to "images/system_settings_plugin/f7cd75a9.png",
        )
    }
}
