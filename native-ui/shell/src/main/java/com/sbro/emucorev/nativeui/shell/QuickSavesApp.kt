package com.sbro.emucorev.nativeui.shell

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import java.io.File
import java.text.DateFormat
import java.util.Date

data class ShellSaveSlot(
    /** 0 = quick save, 1..n = numbered slots. */
    val slot: Int,
    val thumbnail: File?,
    val timestampMillis: Long,
    val sizeBytes: Long,
)

data class ShellSaveGame(
    val titleId: String,
    val title: String,
    val icon: File?,
    val slots: List<ShellSaveSlot>,
)

interface ShellSaveSource {
    fun load(): List<ShellSaveGame>
    /** Starts the title from the chosen save state. */
    fun resume(titleId: String, slot: Int)
}

/**
 * Quick Saves: an EmuCoreV addition (the Vita has no save states), styled as
 * a Vita system application — Trophy Collection list buttons (90 px rows,
 * 27265be7 / 411b968d), 1x PAF text, the Settings-style 64 px header and the
 * firmware corner back button.
 */
class QuickSavesApp(private val source: ShellSaveSource) : ShellApp(TITLE_ID) {
    private var games: List<ShellSaveGame> = emptyList()
    private var openGame = -1

    override fun open(host: ShellAppHost) {
        games = runCatching { source.load() }.getOrDefault(emptyList()).filter { it.slots.isNotEmpty() }
            .sortedByDescending { game -> game.slots.maxOf { it.timestampMillis } }
        openGame = -1
        host.show(rootPage(host), ShellTransition.OPEN)
    }

    override fun onRow(host: ShellAppHost, key: Int, row: Int) {
        when (key) {
            ROOT -> if (row in games.indices) {
                openGame = row
                host.show(gamePage(host, games[row]), ShellTransition.PUSH)
            }
            GAME -> games.getOrNull(openGame)?.let { game ->
                game.slots.getOrNull(row)?.let { source.resume(game.titleId, it.slot) }
            }
        }
    }

    override fun onBack(host: ShellAppHost, key: Int) {
        if (key == GAME) host.show(rootPage(host, openGame), ShellTransition.POP) else host.close()
    }

    private fun rootPage(host: ShellAppHost, focus: Int = -1): ShellPage {
        val text = PafText(host.resources)
        val rows = if (games.isEmpty()) listOf(messageRow(text, host.string(R.string.native_ui_saves_none)))
            else games.map { gameRow(host, text, it) }
        return page(host, text, ROOT, host.string(R.string.native_ui_saves), rows, focus)
    }

    private fun gamePage(host: ShellAppHost, game: ShellSaveGame): ShellPage {
        val text = PafText(host.resources)
        return page(host, text, GAME, game.title, game.slots.map { slotRow(host, text, it) })
    }

    private fun page(host: ShellAppHost, text: PafText, key: Int, title: String, rows: List<ShellRow>,
        focus: Int = -1): ShellPage {
        val header = Bitmap.createBitmap(960, 64, Bitmap.Config.ARGB_8888)
        val c = Canvas(header)
        text.line(c, title, 480f, 32f, 760f, text.paint(32f, bold = true), Paint.Align.CENTER)
        c.drawTexture(host.texture(SettingsApp.SEPARATOR), RectF(0f, 62f, 960f, 64f))
        return ShellPage(TITLE_ID, key, BACKGROUND, header, floatArrayOf(0f, 32f, 960f, 64f),
            floatArrayOf(0f, 96f, 960f, 448f), rows, focus)
    }

    private fun gameRow(host: ShellAppHost, text: PafText, game: ShellSaveGame): ShellRow {
        val bitmap = Bitmap.createBitmap(960, 90, Bitmap.Config.ARGB_8888)
        val c = Canvas(bitmap)
        decodeFile(game.icon)?.let { icon ->
            val s = minOf(160f / icon.width, 88f / icon.height)
            val w = icon.width * s; val h = icon.height * s
            c.drawTexture(icon, RectF(170f - w / 2, 45f - h / 2, 170f + w / 2, 45f + h / 2))
            icon.recycle()
        }
        text.line(c, game.title, 260f, 34f, 610f, text.paint(24f, bold = true))
        val latest = game.slots.maxOf { it.timestampMillis }
        val info = "${host.string(R.string.native_ui_saves_count).format(game.slots.size)}  ·  ${date(latest)}"
        text.line(c, info, 260f, 66f, 610f, text.paint(20f))
        return ShellRow(bitmap, TrophyCollectionApp.ROW_PLATE, TrophyCollectionApp.ROW_GLOW, 0f, 960f, 90f)
    }

    private fun slotRow(host: ShellAppHost, text: PafText, slot: ShellSaveSlot): ShellRow {
        val bitmap = Bitmap.createBitmap(960, 90, Bitmap.Config.ARGB_8888)
        val c = Canvas(bitmap)
        val box = RectF(90f, 3f, 250f, 87f)
        val thumb = slot.thumbnail?.let { ShellThemes.decode(it, 512) }
        if (thumb != null) {
            val s = maxOf(box.width() / thumb.width, box.height() / thumb.height)
            val w = box.width() / s; val h = box.height() / s
            val src = android.graphics.Rect(((thumb.width - w) / 2).toInt(), ((thumb.height - h) / 2).toInt(),
                ((thumb.width + w) / 2).toInt(), ((thumb.height + h) / 2).toInt())
            c.drawBitmap(thumb, src, box, Paint(Paint.FILTER_BITMAP_FLAG))
            thumb.recycle()
        } else {
            c.drawRect(box, Paint().apply { color = 0x66000000 })
        }
        c.drawTexture(host.texture(TrophyCollectionApp.ICON_FRAME), RectF(box.left - 2f, box.top - 2f, box.right + 2f, box.bottom + 2f), alpha = 160)
        val name = if (slot.slot == 0) host.string(R.string.native_ui_saves_quick)
            else host.string(R.string.native_ui_saves_slot).format(slot.slot)
        text.line(c, name, 270f, 34f, 590f, text.paint(24f, bold = true))
        text.line(c, "${date(slot.timestampMillis)}  ·  ${size(slot.sizeBytes)}", 270f, 66f, 590f, text.paint(20f))
        return ShellRow(bitmap, TrophyCollectionApp.ROW_PLATE, TrophyCollectionApp.ROW_GLOW, 0f, 960f, 90f)
    }

    private fun messageRow(text: PafText, message: String): ShellRow {
        val bitmap = Bitmap.createBitmap(960, 120, Bitmap.Config.ARGB_8888)
        text.line(Canvas(bitmap), message, 480f, 60f, 760f, text.paint(28f), Paint.Align.CENTER)
        return ShellRow(bitmap, 0, 0, 0f, 960f, 120f, selectable = false)
    }

    private fun date(millis: Long) =
        DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(millis))

    private fun size(bytes: Long) = when {
        bytes >= 1L shl 20 -> "%.1f MB".format(bytes / 1048576.0)
        else -> "%d KB".format(bytes / 1024)
    }

    companion object {
        const val TITLE_ID = "ECVS00001"
        private const val ROOT = 0
        private const val GAME = 1
        const val BACKGROUND = 0x45c50001
        val TEXTURES = mapOf(BACKGROUND to "system-apps/ECVS00001/app-bg.png")
    }
}
