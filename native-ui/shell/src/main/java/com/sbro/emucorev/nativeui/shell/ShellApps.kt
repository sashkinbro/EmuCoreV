package com.sbro.emucorev.nativeui.shell

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.Typeface
import android.text.TextPaint
import android.text.TextUtils
import java.io.File

/** One list row: a host raster drawn over the PAF button planes. */
class ShellRow(
    val bitmap: Bitmap?,
    val plate: Int,
    val glow: Int,
    val x: Float,
    val width: Float,
    val height: Float,
    val selectable: Boolean = true,
)

class ShellPage(
    val app: String,
    val key: Int,
    val background: Int,
    val header: Bitmap?,
    val headerRect: FloatArray,
    val listRect: FloatArray,
    val rows: List<ShellRow>,
    val focus: Int = -1,
)

enum class ShellTransition(val code: Int) { NONE(0), PUSH(1), POP(2), REPLACE(3), OPEN(4) }

/** What a shell application may ask of the shell. */
interface ShellAppHost {
    val resources: File
    fun show(page: ShellPage, transition: ShellTransition)
    fun close()
    fun string(id: Int): String
    /** Locale-aware, localized file size ("1.5 MB"). */
    fun formatSize(bytes: Long): String
    fun texture(id: Int): Bitmap?
    fun playSound(id: Int)
}

/** A full-screen application started from its LiveArea gate (Start). */
abstract class ShellApp(val titleId: String) {
    abstract fun open(host: ShellAppHost)
    open fun onRow(host: ShellAppHost, key: Int, row: Int) = Unit
    open fun onBack(host: ShellAppHost, key: Int) = host.close()
}

/**
 * PAF text styles. The firmware font sizes are scaled by 19/18, the factor
 * measured for the Home title against emulated SceShell frames; bold=1 uses
 * Rodin Bold (ltn4), otherwise Rodin Regular (ltn0).
 */
class PafText(resources: File) {
    private val regular = Typeface.createFromFile(File(resources, "fonts/ltn0.otf"))
    private val bold = Typeface.createFromFile(File(resources, "fonts/ltn4.otf"))

    fun paint(size: Float, bold: Boolean = false, color: Int = Color.WHITE, shadow: Boolean = false) =
        TextPaint(Paint.ANTI_ALIAS_FLAG).apply {
            this.color = color
            textSize = size * SIZE_SCALE
            typeface = if (bold) this@PafText.bold else regular
            if (shadow) setShadowLayer(1.6f, 0f, 1.2f, 0xC0000000.toInt())
        }

    /** Draws single-line text left-aligned, vertically centred on [centerY], ellipsized. */
    fun line(canvas: Canvas, text: String, x: Float, centerY: Float, width: Float, paint: TextPaint,
        align: Paint.Align = Paint.Align.LEFT) {
        val fitted = TextUtils.ellipsize(text, paint, width, TextUtils.TruncateAt.END).toString()
        val w = paint.measureText(fitted)
        val left = when (align) {
            Paint.Align.CENTER -> x - w * .5f
            Paint.Align.RIGHT -> x - w
            else -> x
        }
        val baseline = centerY - (paint.descent() + paint.ascent()) * .5f
        canvas.drawText(fitted, left, baseline, paint)
    }

    companion object { const val SIZE_SCALE = 19f / 18f }
}

/** Draws a texture stretched into [dst], as a PAF planeobj without div. */
fun Canvas.drawTexture(texture: Bitmap?, dst: RectF, alpha: Int = 255) {
    texture ?: return
    val paint = Paint(Paint.FILTER_BITMAP_FLAG).apply { this.alpha = alpha }
    drawBitmap(texture, Rect(0, 0, texture.width, texture.height), dst, paint)
}

fun decodeFile(file: File?): Bitmap? =
    file?.takeIf(File::isFile)?.let { runCatching { BitmapFactory.decodeFile(it.path) }.getOrNull() }
