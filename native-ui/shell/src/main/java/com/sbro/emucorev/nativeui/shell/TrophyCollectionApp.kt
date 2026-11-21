package com.sbro.emucorev.nativeui.shell

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.ColorMatrix
import android.graphics.ColorMatrixColorFilter
import android.graphics.Paint
import android.graphics.RectF
import java.io.File
import java.text.DateFormat
import java.util.Date

/** Real trophy data supplied by the host application. */
data class ShellTrophy(
    val name: String,
    val detail: String,
    /** 0 platinum, 1 gold, 2 silver, 3 bronze. */
    val grade: Int,
    val hidden: Boolean,
    val unlocked: Boolean,
    val unlockedAtEpochSeconds: Long?,
    val icon: File?,
)

data class ShellTrophyGame(
    val id: String,
    val title: String,
    val icon: File?,
    val trophies: List<ShellTrophy>,
) {
    val earned get() = trophies.count { it.unlocked }
    val progress get() = if (trophies.isEmpty()) 0 else earned * 100 / trophies.size
    fun earned(grade: Int) = trophies.count { it.unlocked && it.grade == grade }
}

fun interface ShellTrophySource {
    fun load(): List<ShellTrophyGame>
}

/**
 * Trophy Collection (NPXS10008), laid out from trophy_plugin /
 * trophy_local_plugin templates: 960x96 header band (y 32..128) with the
 * system/network tabs and the grade legend panel, 90 px list items with the
 * set icon (160x88), title (24 bold), progress bar (200x12) and per-grade
 * counts aligned under the legend; trophy rows with an 88 px framed icon,
 * grade icon, name (24 bold) and detail (20).
 */
class TrophyCollectionApp(private val source: ShellTrophySource) : ShellApp(TITLE_ID) {
    private var games: List<ShellTrophyGame> = emptyList()
    private var openGame = -1
    private var openTrophy = -1

    override fun open(host: ShellAppHost) {
        games = runCatching { source.load() }.getOrDefault(emptyList())
            .filter { it.trophies.isNotEmpty() }
            .sortedBy { it.title.lowercase() }
        openGame = -1
        host.show(rootPage(host), ShellTransition.OPEN)
    }

    override fun onRow(host: ShellAppHost, key: Int, row: Int) {
        when (key) {
            ROOT -> if (row in games.indices) {
                openGame = row
                host.show(gamePage(host, row), ShellTransition.PUSH)
            }
            GAME -> {
                val game = games.getOrNull(openGame) ?: return
                if (row in game.trophies.indices) {
                    openTrophy = row
                    host.show(detailPage(host, game, row), ShellTransition.PUSH)
                }
            }
        }
    }

    override fun onBack(host: ShellAppHost, key: Int) {
        when (key) {
            DETAIL -> games.getOrNull(openGame)?.let { host.show(gamePage(host, openGame, openTrophy), ShellTransition.POP) }
            GAME -> host.show(rootPage(host, openGame), ShellTransition.POP)
            else -> host.close()
        }
    }

    // ---------------------------------------------------------------- pages

    private fun rootPage(host: ShellAppHost, focus: Int = -1): ShellPage {
        val text = PafText(host.resources)
        val header = header(host, text, games.sumOf { it.trophies.size }, games.sumOf { it.earned },
            IntArray(4) { grade -> games.sumOf { it.earned(grade) } })
        val rows = games.map { game -> gameRow(host, text, game) }
        return ShellPage(TITLE_ID, ROOT, BACKGROUND, header, floatArrayOf(0f, 32f, 960f, 96f),
            LIST_RECT, rows, focus)
    }

    private fun gamePage(host: ShellAppHost, index: Int, focus: Int = -1): ShellPage {
        val game = games[index]
        val text = PafText(host.resources)
        val header = header(host, text, game.trophies.size, game.earned, IntArray(4) { game.earned(it) }, game)
        val rows = game.trophies.map { trophyRow(host, text, it) }
        return ShellPage(TITLE_ID, GAME, BACKGROUND, header, floatArrayOf(0f, 32f, 960f, 96f),
            LIST_RECT, rows, focus)
    }

    private fun detailPage(host: ShellAppHost, game: ShellTrophyGame, index: Int): ShellPage {
        val trophy = game.trophies[index]
        val text = PafText(host.resources)
        val header = header(host, text, game.trophies.size, game.earned, IntArray(4) { game.earned(it) }, game)
        val rows = ArrayList<ShellRow>()
        rows += ShellRow(trophyRowBitmap(host, text, trophy, large = true), 0, 0, 0f, 960f, 120f, false)
        // Label/value rows (templates 0b446e15 / 065f1822): 28 px label at x 90,
        // value from x 360.
        val date = trophy.unlockedAtEpochSeconds?.takeIf { trophy.unlocked }?.let {
            DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(it * 1000))
        } ?: host.string(R.string.native_ui_trophy_not_earned)
        rows += labelRow(text, host.string(R.string.native_ui_trophy_grade), gradeName(host, trophy.grade), host.texture(SMALL_CUPS[trophy.grade]))
        rows += labelRow(text, host.string(R.string.native_ui_trophy_earned), date, null)
        if (!trophy.hidden || trophy.unlocked)
            rows += labelRow(text, host.string(R.string.native_ui_trophy_details), trophy.detail, null, multiline = true)
        return ShellPage(TITLE_ID, DETAIL, BACKGROUND, header, floatArrayOf(0f, 32f, 960f, 96f),
            LIST_RECT, rows)
    }

    // ---------------------------------------------------------------- rasters

    private fun header(host: ShellAppHost, text: PafText, total: Int, earned: Int, perGrade: IntArray,
        game: ShellTrophyGame? = null): Bitmap {
        val bitmap = Bitmap.createBitmap(960, 96, Bitmap.Config.ARGB_8888)
        val c = Canvas(bitmap)
        c.drawTexture(host.texture(HEADER_BAND), RectF(0f, 0f, 960f, 96f))
        if (game == null) {
            // Tabs (page 5fcd170e): 172x48 buttons from x 8 and 180, y 8 in the band.
            c.drawTexture(host.texture(TAB_SYSTEM), RectF(8f, 8f, 180f, 56f))
            c.drawTexture(host.texture(TAB_NETWORK), RectF(180f, 8f, 352f, 56f), alpha = 110)
            // Tab labels (style d3da8ad9): 16 px bold, two lines at 17 px pitch.
            val tab = text.paint(16f, bold = true, shadow = true)
            tabLabel(text, c, host.string(R.string.native_ui_trophy_system), 70f, 102f, tab)
            tab.alpha = 110
            tabLabel(text, c, host.string(R.string.native_ui_trophy_psn), 235f, 111f, tab)
        } else {
            val title = text.paint(24f, bold = true, shadow = true)
            text.line(c, game.title, 16f, 30f, 560f, title)
        }
        // Progress under the tabs (template 54902803): label right edge 241,
        // 64x12 bar to x 310, percentage from 315; centre y 80 (band y 48).
        val small = text.paint(16f, bold = true, shadow = true)
        text.line(c, host.string(R.string.native_ui_trophy_progress), 241f, 76f, 233f, small, Paint.Align.RIGHT)
        progress(host, c, RectF(246f, 70f, 310f, 82f), if (total == 0) 0f else earned / total.toFloat())
        text.line(c, "${if (total == 0) 0 else earned * 100 / total}%", 315f, 76f, 60f, small)
        // Grade legend panel (page c3b32488): 182x66 at centre (759, 57).
        c.drawTexture(host.texture(LEGEND), RectF(668f, 24f, 850f, 90f))
        val count = text.paint(20f, bold = true, shadow = true)
        LEGEND_COLUMNS.forEachIndexed { grade, x ->
            text.line(c, perGrade[grade].toString(), x, 80f, 50f, count, Paint.Align.CENTER)
        }
        return bitmap
    }

    private fun tabLabel(text: PafText, c: Canvas, label: String, x: Float, width: Float,
        paint: android.text.TextPaint) {
        val lines = if (paint.measureText(label) <= width) listOf(label)
            else wrap(label.replace("™", "™ "), paint, width).take(2)
        val top = 32f - (lines.size - 1) * 8.5f
        lines.forEachIndexed { i, line -> text.line(c, line, x, top + i * 17f, width, paint) }
    }

    private fun gameRow(host: ShellAppHost, text: PafText, game: ShellTrophyGame): ShellRow {
        val bitmap = Bitmap.createBitmap(960, 90, Bitmap.Config.ARGB_8888)
        val c = Canvas(bitmap)
        decodeFile(game.icon)?.let { icon ->
            c.drawTexture(icon, fitInto(icon, RectF(90f, 1f, 250f, 89f)))
            icon.recycle()
        }
        text.line(c, game.title, 260f, 34f, 370f, text.paint(24f, bold = true))
        progress(host, c, RectF(260f, 56f, 460f, 68f), game.progress / 100f)
        text.line(c, "${game.progress}%", 470f, 62f, 60f, text.paint(20f, bold = true))
        if (game.earned(0) > 0) c.drawTexture(host.texture(SMALL_CUPS[0]), RectF(521f, 49f, 547f, 75f))
        c.drawTexture(host.texture(COUNT_BOX), RectF(649f, 9f, 871f, 81f))
        val count = text.paint(24f, bold = true)
        LEGEND_COLUMNS.forEachIndexed { grade, x ->
            text.line(c, game.earned(grade).toString(), x, 55f, 50f, count, Paint.Align.CENTER)
        }
        return ShellRow(bitmap, ROW_PLATE, ROW_GLOW, 0f, 960f, 90f)
    }

    private fun trophyRow(host: ShellAppHost, text: PafText, trophy: ShellTrophy): ShellRow =
        ShellRow(trophyRowBitmap(host, text, trophy, large = false), ROW_PLATE, ROW_GLOW, 0f, 960f, 90f)

    private fun trophyRowBitmap(host: ShellAppHost, text: PafText, trophy: ShellTrophy, large: Boolean): Bitmap {
        val height = if (large) 120 else 90
        val bitmap = Bitmap.createBitmap(960, height, Bitmap.Config.ARGB_8888)
        val c = Canvas(bitmap)
        val cy = height / 2f
        val iconRect = RectF(126f, cy - 44f, 214f, cy + 44f)
        val secret = trophy.hidden && !trophy.unlocked
        val icon = if (secret) null else decodeFile(trophy.icon)
        if (icon != null) {
            val paint = Paint(Paint.FILTER_BITMAP_FLAG)
            if (!trophy.unlocked) paint.colorFilter = ColorMatrixColorFilter(ColorMatrix().apply { setSaturation(0f) })
                .also { paint.alpha = 150 }
            c.drawBitmap(icon, null, iconRect, paint)
            icon.recycle()
        } else {
            c.drawTexture(host.texture(HIDDEN_ICON), RectF(138f, cy - 32f, 202f, cy + 32f), alpha = 200)
        }
        c.drawTexture(host.texture(ICON_FRAME), iconRect)
        c.drawTexture(host.texture(SMALL_CUPS[trophy.grade.coerceIn(0, 3)]), RectF(227f, cy - 26f, 253f, cy))
        val name = if (secret) host.string(R.string.native_ui_trophy_hidden) else trophy.name
        val namePaint = text.paint(24f, bold = true)
        if (!trophy.unlocked) namePaint.alpha = 170
        text.line(c, name, 260f, cy - 6f, 610f, namePaint)
        if (!secret) {
            val detail = text.paint(20f)
            if (!trophy.unlocked) detail.alpha = 170
            text.line(c, trophy.detail, 260f, cy + 24f, 610f, detail)
        }
        return bitmap
    }

    private fun labelRow(text: PafText, label: String, value: String, icon: Bitmap?, multiline: Boolean = false): ShellRow {
        val valuePaint = text.paint(28f)
        val lines = if (multiline) wrap(value, valuePaint, 510f).take(6) else listOf(value)
        val height = 60f + (lines.size - 1) * 40f
        val bitmap = Bitmap.createBitmap(960, height.toInt(), Bitmap.Config.ARGB_8888)
        val c = Canvas(bitmap)
        text.line(c, label, 90f, 30f, 270f, text.paint(28f))
        var x = 360f
        if (icon != null) { c.drawTexture(icon, RectF(360f, 17f, 386f, 43f)); x = 396f }
        lines.forEachIndexed { i, line -> text.line(c, line, x, 30f + i * 40f, 510f, valuePaint) }
        return ShellRow(bitmap, 0, 0, 0f, 960f, height, false)
    }

    private fun wrap(value: String, paint: android.text.TextPaint, width: Float): List<String> {
        val layout = android.text.StaticLayout.Builder.obtain(value, 0, value.length, paint, width.toInt()).build()
        return (0 until layout.lineCount).map { value.substring(layout.getLineStart(it), layout.getLineEnd(it)).trim() }
    }

    private fun progress(host: ShellAppHost, c: Canvas, rect: RectF, fraction: Float) {
        c.drawTexture(host.texture(PROGRESS_BASE), rect)
        val fore = host.texture(PROGRESS_FORE) ?: return
        val filled = RectF(rect.left, rect.top, rect.left + rect.width() * fraction.coerceIn(0f, 1f), rect.bottom)
        if (filled.width() <= 0f) return
        // style_progressbar 286ba040: bar colour (0.22, 0.5, 0.78).
        val paint = Paint(Paint.FILTER_BITMAP_FLAG).apply {
            colorFilter = ColorMatrixColorFilter(ColorMatrix().apply { setScale(0.22f, 0.5f, 0.78f, 1f) })
        }
        c.drawBitmap(fore, null, filled, paint)
    }

    private fun gradeName(host: ShellAppHost, grade: Int) =
        GRADE_NAMES.getOrNull(grade)?.let(host::string).orEmpty()

    private fun fitInto(bitmap: Bitmap, box: RectF): RectF {
        val s = minOf(box.width() / bitmap.width, box.height() / bitmap.height)
        val w = bitmap.width * s; val h = bitmap.height * s
        return RectF(box.centerX() - w / 2, box.centerY() - h / 2, box.centerX() + w / 2, box.centerY() + h / 2)
    }

    companion object {
        const val TITLE_ID = "NPXS10008"
        private const val ROOT = 0
        private const val GAME = 1
        private const val DETAIL = 2
        private val LIST_RECT = floatArrayOf(0f, 128f, 960f, 416f)
        private val LEGEND_COLUMNS = floatArrayOf(684f, 734f, 784f, 834f)
        private val GRADE_NAMES = intArrayOf(R.string.native_ui_trophy_platinum, R.string.native_ui_trophy_gold,
            R.string.native_ui_trophy_silver, R.string.native_ui_trophy_bronze).toList()

        const val BACKGROUND = 0xe17dc604.toInt()
        const val HEADER_BAND = 0x5ef7041d
        const val TAB_SYSTEM = 0x6c00de56
        const val TAB_NETWORK = 0xae6173d4.toInt()
        const val LEGEND = 0xf1572d13.toInt()
        const val COUNT_BOX = 0x1d602a80
        const val ICON_FRAME = 0x3e2de608
        const val HIDDEN_ICON = 0x8f397158.toInt()
        const val ROW_PLATE = 0x27265be7
        const val ROW_GLOW = 0x411b968d
        const val PROGRESS_BASE = 0x53e25bfa
        const val PROGRESS_FORE = 0x301ef5d2
        val SMALL_CUPS = intArrayOf(0xabdf7c9d.toInt(), 0xdf6d6000.toInt(), 0xb99d971a.toInt(), 0x31a2b441)

        /** Pack paths of every texture this application uses. */
        val TEXTURES = mapOf(
            BACKGROUND to "images/trophy_plugin/e17dc604.png",
            HEADER_BAND to "images/trophy_plugin/5ef7041d.png",
            TAB_SYSTEM to "images/trophy_plugin/6c00de56.png",
            TAB_NETWORK to "images/trophy_plugin/ae6173d4.png",
            LEGEND to "images/trophy_local_plugin/f1572d13.png",
            COUNT_BOX to "images/trophy_local_plugin/1d602a80.png",
            ICON_FRAME to "images/trophy_local_plugin/3e2de608.png",
            HIDDEN_ICON to "images/trophy_plugin/8f397158.png",
            ROW_PLATE to "images/trophy_local_plugin/27265be7.png",
            ROW_GLOW to "images/trophy_local_plugin/411b968d.png",
            PROGRESS_BASE to "images/paf-common-resource/53e25bfa.png",
            PROGRESS_FORE to "images/paf-common-resource/301ef5d2.png",
            SMALL_CUPS[0] to "images/trophy_plugin/abdf7c9d.png",
            SMALL_CUPS[1] to "images/trophy_plugin/df6d6000.png",
            SMALL_CUPS[2] to "images/trophy_plugin/b99d971a.png",
            SMALL_CUPS[3] to "images/trophy_plugin/31a2b441.png",
        )
    }
}
