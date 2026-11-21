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
    /** Sound & Display > System Music: the theme's Home BGM. */
    var systemMusic: Boolean
        get() = prefs.getBoolean("system_music", true)
        set(value) = prefs.edit().putBoolean("system_music", value).apply()
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
    /** Installed custom themes (id to title) and the active one. */
    val customThemes: List<Pair<String, String>>
    val activeCustomTheme: String?
    fun selectWaveTheme(name: String)
    fun selectCustomTheme(id: String)
    fun installTheme()
    val hasHomePhoto: Boolean
    val hasStartPhoto: Boolean
    fun pickHomePhoto()
    fun pickStartPhoto()
    fun clearHomePhoto()
    fun clearStartPhoto()
    fun deleteTheme(id: String)
    fun applyMusic()
    fun themeProvider(id: String?): String?
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
    private var detailTheme: String? = null
    private var confirmDelete = false
    @Volatile private var currentKey = -1
    @Volatile private var currentHost: ShellAppHost? = null

    /** Rebuilds the visible page after an asynchronous change (theme install, photo pick). */
    fun refresh() {
        val host = currentHost ?: return
        if (currentKey >= 0) host.show(page(host, currentKey), ShellTransition.REPLACE)
    }

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
                0 -> push(host, THEME_BACKGROUND)
                1 -> push(host, SOUND_DISPLAY)
                2 -> push(host, DATE_TIME)
                3 -> push(host, SYSTEM)
                4 -> actions.exitMode()
            }
            THEME_BACKGROUND -> when (row) {
                0 -> push(host, THEME)
                1 -> push(host, HOME_BACKGROUNDS)
                2 -> push(host, START_SCREEN)
            }
            THEME -> {
                // Default, the standard wave colour themes, installed themes, install.
                val custom = actions.customThemes
                val first = 1 + STANDARD_THEMES.size
                when {
                    row == 0 -> actions.selectWaveTheme(ShellSettings.DEFAULT_THEME)
                    row < first -> actions.selectWaveTheme(STANDARD_THEMES[row - 1].first)
                    row - first < custom.size -> {
                        val id = custom[row - first].first
                        if (id == actions.activeCustomTheme) {
                            // Tapping the theme in use opens its page (information, delete).
                            detailTheme = id
                            confirmDelete = false
                            push(host, THEME_DETAIL)
                            return
                        }
                        actions.selectCustomTheme(id)
                    }
                    else -> { actions.installTheme(); return }
                }
                host.show(page(host, THEME), ShellTransition.REPLACE)
            }
            HOME_BACKGROUNDS -> if (row == 0) { actions.clearHomePhoto(); host.show(page(host, key), ShellTransition.REPLACE) }
                else actions.pickHomePhoto()
            START_SCREEN -> if (row == 0) { actions.clearStartPhoto(); host.show(page(host, key), ShellTransition.REPLACE) }
                else actions.pickStartPhoto()
            THEME_DETAIL -> if (row == 1) {
                val id = detailTheme ?: return
                if (!confirmDelete) {
                    confirmDelete = true
                    host.show(page(host, THEME_DETAIL), ShellTransition.REPLACE)
                } else {
                    actions.deleteTheme(id)
                    detailTheme = null
                    host.show(page(host, THEME), ShellTransition.POP)
                }
            }
            SOUND_DISPLAY -> {
                when (row) {
                    0 -> { s.systemMusic = !s.systemMusic; actions.applyMusic() }
                    1 -> s.systemSounds = !s.systemSounds
                    2 -> { push(host, DISPLAY_AREA); return }
                }
                host.show(page(host, SOUND_DISPLAY), ShellTransition.REPLACE)
            }
            DISPLAY_AREA -> if (row in 0..1) {
                s.displayArea = row
                actions.applyDisplayArea(row)
                host.show(page(host, DISPLAY_AREA), ShellTransition.REPLACE)
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
            THEME_DETAIL -> host.show(page(host, THEME), ShellTransition.POP)
            DISPLAY_AREA -> host.show(page(host, SOUND_DISPLAY), ShellTransition.POP)
            THEME, HOME_BACKGROUNDS, START_SCREEN -> host.show(page(host, THEME_BACKGROUND), ShellTransition.POP)
            else -> host.show(page(host, ROOT), ShellTransition.POP)
        }
    }

    private fun push(host: ShellAppHost, key: Int) = host.show(page(host, key), ShellTransition.PUSH)

    private fun page(host: ShellAppHost, key: Int): ShellPage {
        currentKey = key
        currentHost = host
        val s = actions.settings
        val (title, items) = when (key) {
            ROOT -> host.string(R.string.native_ui_settings) to listOf(
                Item(host.string(R.string.native_ui_set_theme_background), ICON_THEME, arrow = true),
                Item(host.string(R.string.native_ui_set_sound_display), ICON_SOUND, arrow = true),
                Item(host.string(R.string.native_ui_set_date_time), ICON_CLOCK, arrow = true),
                Item(host.string(R.string.native_ui_set_system), ICON_SYSTEM, arrow = true),
                Item(host.string(R.string.native_ui_set_exit_mode), ICON_EXIT),
            )
            THEME_BACKGROUND -> host.string(R.string.native_ui_set_theme_background) to listOf(
                Item(host.string(R.string.native_ui_set_theme), arrow = true),
                Item(host.string(R.string.native_ui_set_home_backgrounds), arrow = true),
                Item(host.string(R.string.native_ui_set_start_screen), arrow = true),
            )
            THEME -> host.string(R.string.native_ui_set_theme) to buildList {
                val custom = actions.activeCustomTheme
                add(Item(host.string(R.string.native_ui_set_default),
                    check = custom == null && s.waveTheme == ShellSettings.DEFAULT_THEME))
                STANDARD_THEMES.forEach { (effect, name) ->
                    add(Item(host.string(name), check = custom == null && s.waveTheme == effect))
                }
                actions.customThemes.forEach { (id, title) -> add(Item(title, check = id == custom)) }
                add(Item(host.string(R.string.native_ui_set_install_theme), arrow = true))
            }
            HOME_BACKGROUNDS -> host.string(R.string.native_ui_set_home_backgrounds) to listOf(
                Item(host.string(R.string.native_ui_set_default), check = !actions.hasHomePhoto),
                Item(host.string(R.string.native_ui_set_choose_image), check = actions.hasHomePhoto),
            )
            START_SCREEN -> host.string(R.string.native_ui_set_start_screen) to listOf(
                Item(host.string(R.string.native_ui_set_default), check = !actions.hasStartPhoto),
                Item(host.string(R.string.native_ui_set_choose_image), check = actions.hasStartPhoto),
            )
            THEME_DETAIL -> {
                val id = detailTheme
                val title = actions.customThemes.firstOrNull { it.first == id }?.second.orEmpty()
                title to buildList {
                    add(Item(if (confirmDelete) host.string(R.string.native_ui_set_delete_theme_confirm)
                        else actions.themeProvider(id).orEmpty().ifBlank { title }, selectable = false))
                    add(Item(host.string(R.string.native_ui_set_delete)))
                }
            }
            SOUND_DISPLAY -> host.string(R.string.native_ui_set_sound_display) to listOf(
                Item(host.string(R.string.native_ui_set_system_music), check = s.systemMusic),
                Item(host.string(R.string.native_ui_set_system_sounds), check = s.systemSounds),
                Item(host.string(R.string.native_ui_set_display_area), arrow = true,
                    value = host.string(if (s.displayArea == 0) R.string.native_ui_set_full else R.string.native_ui_set_original)),
            )
            DISPLAY_AREA -> host.string(R.string.native_ui_set_display_area) to listOf(
                Item(host.string(R.string.native_ui_set_full), check = s.displayArea == 0),
                Item(host.string(R.string.native_ui_set_original), check = s.displayArea == 1),
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
        val valuePaint = text.paint(20f, bold = true)
        val valueWidth = item.value?.let { minOf(300f, valuePaint.measureText(it) + 8f) } ?: 0f
        val label = text.paint(28f, bold = true)
        if (!item.selectable && item.value == null) {
            // Free text row (easter egg): wrap over the full width.
            text.line(c, item.label, labelX, 40f, 840f - labelX - 24f, text.paint(20f, bold = true))
        } else {
            text.line(c, item.label, labelX, 40f, 840f - labelX - trailing - valueWidth - 16f, label)
        }
        item.value?.let {
            text.line(c, it, 840f - trailing, 40f, valueWidth, valuePaint, Paint.Align.RIGHT)
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
        private const val THEME_BACKGROUND = 6
        private const val HOME_BACKGROUNDS = 7
        private const val START_SCREEN = 8
        private const val THEME_DETAIL = 9
        private const val DISPLAY_AREA = 10

        /** Standard wave colour themes: libpaf effect and localized colour name. */
        val STANDARD_THEMES = listOf(
            "effects_black" to R.string.native_ui_color_black,
            "effects_blueTB_0513_01" to R.string.native_ui_color_blue,
            "effects_bluewhite_05" to R.string.native_ui_color_light_blue,
            "effects_turquoise_0513_01" to R.string.native_ui_color_turquoise,
            "effects_green_0512_01" to R.string.native_ui_color_green,
            "effects_limegreen_0513_02" to R.string.native_ui_color_lime,
            "effects_yellow_0512_01" to R.string.native_ui_color_yellow,
            "effects_marrygold" to R.string.native_ui_color_marigold,
            "effects_0408_01_orange" to R.string.native_ui_color_orange,
            "effects_redTB_0513_01" to R.string.native_ui_color_red,
            "effects_0406_ruby" to R.string.native_ui_color_ruby,
            "effects_peach_0512_001" to R.string.native_ui_color_peach,
            "effects_purplered" to R.string.native_ui_color_magenta,
            "effects_purple_01" to R.string.native_ui_color_purple,
            "effects_0407_purple_01" to R.string.native_ui_color_violet,
            "effects_0518_gray_FLAT" to R.string.native_ui_color_gray,
        )

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
