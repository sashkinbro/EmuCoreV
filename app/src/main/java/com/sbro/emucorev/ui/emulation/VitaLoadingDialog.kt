package com.sbro.emucorev.ui.emulation

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.ColorMatrix
import android.graphics.ColorMatrixColorFilter
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.Typeface
import android.text.Layout
import android.text.StaticLayout
import android.text.TextPaint
import androidx.compose.foundation.Canvas as ComposeCanvas
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.nativeCanvas
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.nativeui.FirmwarePack
import java.io.File
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

/**
 * Loading dialog drawn like the Vita system progress dialog, from the resource
 * pack that the application extracts on the device from the user's firmware.
 * Nothing here ships firmware artwork: without the pack [VitaDialogAssets.load]
 * returns null and the caller keeps the regular loading card.
 *
 * Recovered values (msg_dialog_plugin.rco template abc820fb and the common
 * resource style 501d508e): dialog box margins 20, message text_box width 500
 * with 26 px / 40 px line pitch, sub text 20 px, progress bar 500 x 30 using
 * 53e25bfa (track) and 301ef5d2 (fill, colour 0.659, 1, 0). Everything marked
 * PROVISIONAL below is not recovered from the firmware.
 */
internal class VitaDialogAssets(
    val frame: Bitmap,
    val barBase: Bitmap,
    val barFore: Bitmap,
    val busyDot: Bitmap,
    val typeface: Typeface,
) {
    companion object {
        private const val COMMON = "images/paf-common-resource"

        /** The built pack of the installed firmware, or null (no firmware / pack not built yet). */
        fun packDirectory(context: Context): File? =
            runCatching { FirmwarePack.readyDirectory(context, EmulatorStorage.vitaRoot(context)) }.getOrNull()

        /** Null when any needed pack file is missing or unreadable. */
        fun load(pack: File): VitaDialogAssets? {
            fun image(path: String): Bitmap? = runCatching {
                File(pack, path).inputStream().use { BitmapFactory.decodeStream(it) }
            }.getOrNull()
            // Pack ltn0: the system UI font.
            val fontFile = listOf("otf", "ttf").map { File(pack, "fonts/ltn0.$it") }.firstOrNull { it.isFile }
                ?: return null
            val typeface = runCatching { Typeface.createFromFile(fontFile) }.getOrNull() ?: return null
            return VitaDialogAssets(
                frame = image("$COMMON/8c2646d7.png") ?: return null,
                barBase = image("$COMMON/53e25bfa.png") ?: return null,
                barFore = image("$COMMON/301ef5d2.png") ?: return null,
                busyDot = image("images/topmenu_plugin/754463d1.png") ?: return null,
                typeface = typeface,
            )
        }
    }
}

/**
 * Kotlin port of emucorev::paf::BusyIndicator (native-ui/paf/src/busy_indicator.cpp), the
 * PAF component that moves the head ball and eight fading trail dots. Same arithmetic.
 */
internal class VitaBusyIndicator(private val width: Float, private val ballSize: Float) {
    class Dot { var x = 0f; var y = 0f; var size = 0f; var alpha = 255 }

    private enum class State { Idle, Starting, Running, Stopping }

    val dots = Array(9) { Dot() }
    private var state = State.Idle
    private var phase = 0f
    private var elapsed = 0f
    private var lastEmission = 0f

    fun start() {
        if (state == State.Idle) {
            state = State.Starting; phase = 0f; elapsed = 0f; lastEmission = 0f
        } else if (state == State.Stopping) {
            state = State.Starting; elapsed = (1f - elapsed * 2f) * .3f
        }
    }

    fun tick(seconds: Float) {
        if (state == State.Idle || !seconds.isFinite() || seconds < 0f) return
        phase += seconds
        if (phase >= PI) { phase -= PI; lastEmission -= SIX_PI }
        val angle = phase * 6f
        var scale = 1f
        var emission = 1f
        var radius = (width - ballSize) * .5f
        if (state == State.Starting) {
            elapsed += seconds
            var rate = elapsed / .3f
            if (rate > 1f) { state = State.Running; rate = 1f }
            scale = sin((rate * HALF_PI).toDouble()).toFloat()
            radius *= 2f - scale
            emission = scale * scale
        }
        val head = dots[0]
        head.x = radius * cos(angle.toDouble()).toFloat()
        head.y = -(radius * sin(angle.toDouble()).toFloat())
        head.size = max(0f, ballSize * scale)
        head.alpha = 255
        var fade = .18f
        var emit = false
        if (emission > 0f) {
            val interval = SPACING * emission
            fade = if (interval > .001f) ((seconds * 6f) * .82f) / (interval * 8f) else 0f
            emit = lastEmission + interval < angle
        }
        for (i in 1 until dots.size) {
            val dot = dots[i]
            val alpha = dot.alpha.toFloat() * INVERSE_BYTE
            if (alpha <= .18f) {
                if (head.size > 0f && emit) {
                    dot.x = head.x; dot.y = head.y; dot.size = head.size * 1.25f; dot.alpha = 255
                    lastEmission = angle
                    emit = false
                } else dot.alpha = 0
            } else {
                if (dot.size > ballSize) {
                    dot.size = if (dot.size - ballSize > .5f) (dot.size + ballSize) * .5f else ballSize
                }
                dot.alpha = ((alpha - fade) * 255f).coerceIn(0f, 255f).toInt()
            }
        }
    }

    private companion object {
        val PI = Float.fromBits(0x40490fdb)
        val HALF_PI = Float.fromBits(0x3fc90fdb)
        val SPACING = Float.fromBits(0x3f20d97c)
        val SIX_PI = Float.fromBits(0x4196cbe4)
        val INVERSE_BYTE = Float.fromBits(0x3b808081)
    }
}

/** Plain-canvas renderer (host-testable). Layout is in 960 x 544 Vita units, scaled to fit. */
internal object VitaLoadingDialogRenderer {
    private const val UNITS_W = 960f
    private const val UNITS_H = 544f
    private const val MARGIN = 20f          // layout_box margins and space of template abc820fb
    private const val CONTENT_W = 500f      // text_box / progressbar width
    private const val MESSAGE_SIZE = 26f    // style e6ea7d34
    private const val MESSAGE_PITCH = 40f
    private const val SUB_SIZE = 20f        // style c6e3b635
    private const val BAR_H = 30f           // progressbar layout_hint size
    private const val BAR_CAP_SRC = 7f      // track/fill art: 36 x 14 pill, 7 px end caps
    private const val BUSY_SIZE = 72f       // topmenu busy profile (native-ui shell.cpp)
    private const val FRAME_BLEED = 7f      // transparent border of frame art 8c2646d7
    private const val FRAME_SLICE = 20f     // PROVISIONAL slice of frame art 8c2646d7 (86 x 74)
    private val FILL_TINT = floatArrayOf(0.659f, 1f, 0f, 1f)   // style 501d508e bar_fore_obj
    private val BUSY_TINT = floatArrayOf(0.66f, 0.9f, 1f)      // shell.cpp busy_ball_tint

    /** [progress] null draws the busy indicator instead of the bar. */
    fun draw(
        canvas: Canvas,
        width: Float,
        height: Float,
        assets: VitaDialogAssets,
        message: String,
        progress: Float?,
        busy: VitaBusyIndicator?,
    ) {
        val s = min(width / UNITS_W, height / UNITS_H)
        val messagePaint = TextPaint(Paint.ANTI_ALIAS_FLAG).apply {
            typeface = assets.typeface; textSize = MESSAGE_SIZE * s; color = 0xFFFFFFFF.toInt()
        }
        val metrics = messagePaint.fontMetrics
        val layout = StaticLayout.Builder
            .obtain(message, 0, message.length, messagePaint, (CONTENT_W * s).toInt().coerceAtLeast(1))
            // PROVISIONAL: centred text (the style's align_x/text_align alone do not prove it).
            .setAlignment(Layout.Alignment.ALIGN_CENTER)
            .setLineSpacing(MESSAGE_PITCH * s - (metrics.descent - metrics.ascent), 1f)
            .setIncludePad(false)
            .build()
        val textH = layout.height.toFloat()
        val subH = if (progress != null) SUB_SIZE * 1.4f * s + MARGIN * s else 0f
        val blockH = (if (progress != null) BAR_H else BUSY_SIZE) * s
        val panelW = (CONTENT_W + 2 * MARGIN) * s
        val panelH = 3 * MARGIN * s + textH + subH + blockH
        val left = (width - panelW) / 2f
        val top = (height - panelH) / 2f

        // PROVISIONAL: dark rounded art 8c2646d7 (centre colour 0.149, 0.157, 0.18 like the
        // palette style bdb0eaff) as the dialog panel; the firmware draws the panel in code.
        drawNinePatch(canvas, assets.frame, FRAME_SLICE, s,
            RectF(left - FRAME_BLEED * s, top - FRAME_BLEED * s, left + panelW + FRAME_BLEED * s, top + panelH + FRAME_BLEED * s),
            null)

        canvas.save()
        canvas.translate(left + MARGIN * s, top + MARGIN * s)
        layout.draw(canvas)
        canvas.restore()

        var y = top + MARGIN * s + textH + MARGIN * s
        val contentLeft = left + MARGIN * s
        if (progress != null) {
            val value = progress.coerceIn(0f, 1f)
            val subPaint = TextPaint(Paint.ANTI_ALIAS_FLAG).apply {
                typeface = assets.typeface; textSize = SUB_SIZE * s; color = 0xFFFFFFFF.toInt()
                textAlign = Paint.Align.CENTER
            }
            canvas.drawText("${(value * 100).toInt()}%", left + panelW / 2f, y + SUB_SIZE * s, subPaint)
            y += subH
            val bar = RectF(contentLeft, y, contentLeft + CONTENT_W * s, y + BAR_H * s)
            drawNinePatch(canvas, assets.barBase, BAR_CAP_SRC, BAR_H / assets.barBase.height * s, bar, null)
            val fill = RectF(bar.left, bar.top, bar.left + bar.width() * value, bar.bottom)
            if (fill.width() > 0f) {
                val tint = Paint(Paint.FILTER_BITMAP_FLAG).apply {
                    colorFilter = ColorMatrixColorFilter(ColorMatrix().apply {
                        setScale(FILL_TINT[0], FILL_TINT[1], FILL_TINT[2], FILL_TINT[3])
                    })
                }
                drawNinePatch(canvas, assets.barFore, BAR_CAP_SRC, BAR_H / assets.barFore.height * s, fill, tint)
            }
        } else if (busy != null) {
            val cx = left + panelW / 2f
            val cy = y + blockH / 2f
            val paint = Paint(Paint.FILTER_BITMAP_FLAG)
            val dst = RectF()
            for (dot in busy.dots) {
                if (dot.size <= 0f || dot.alpha == 0) continue
                val size = dot.size * s
                dst.set(cx + dot.x * s - size / 2f, cy - dot.y * s - size / 2f,
                    cx + dot.x * s + size / 2f, cy - dot.y * s + size / 2f)
                paint.colorFilter = ColorMatrixColorFilter(ColorMatrix(floatArrayOf(
                    BUSY_TINT[0], 0f, 0f, 0f, 0f,
                    0f, BUSY_TINT[1], 0f, 0f, 0f,
                    0f, 0f, BUSY_TINT[2], 0f, 0f,
                    0f, 0f, 0f, dot.alpha / 255f, 0f)))
                canvas.drawBitmap(assets.busyDot, null, dst, paint)
            }
        }
    }

    /**
     * Horizontal and vertical 3-slice with [slice] source pixels at each edge, drawn
     * at [unit] destination pixels per source pixel (corners keep the art's aspect).
     */
    private fun drawNinePatch(canvas: Canvas, bitmap: Bitmap, slice: Float, unit: Float, dst: RectF, paint: Paint?) {
        val p = paint ?: Paint(Paint.FILTER_BITMAP_FLAG)
        val sw = bitmap.width
        val sh = bitmap.height
        val edgeX = min(slice, sw / 2f)
        val edgeY = if (slice >= sh / 2f || sh <= 16) 0f else min(slice, sh / 2f)
        val dx = min(edgeX * unit, dst.width() / 2f)
        val dy = min(edgeY * unit, dst.height() / 2f)
        val xs = floatArrayOf(dst.left, dst.left + dx, dst.right - dx, dst.right)
        val ys = floatArrayOf(dst.top, dst.top + dy, dst.bottom - dy, dst.bottom)
        val sxs = intArrayOf(0, edgeX.toInt(), sw - edgeX.toInt(), sw)
        val sys = if (edgeY == 0f) intArrayOf(0, 0, sh, sh) else intArrayOf(0, edgeY.toInt(), sh - edgeY.toInt(), sh)
        val src = Rect()
        val out = RectF()
        for (r in 0 until 3) for (c in 0 until 3) {
            src.set(sxs[c], sys[r], sxs[c + 1], sys[r + 1])
            out.set(xs[c], ys[r], xs[c + 1], ys[r + 1])
            if (src.width() <= 0 || src.height() <= 0 || out.width() <= 0f || out.height() <= 0f) continue
            canvas.drawBitmap(bitmap, src, out, p)
        }
    }
}

/** [progress] null shows the animated busy indicator. */
@Composable
internal fun VitaLoadingDialog(
    assets: VitaDialogAssets,
    message: String,
    progress: Float?,
    modifier: Modifier = Modifier,
) {
    val busy = remember { VitaBusyIndicator(72f, 16f).apply { start() } }
    var frame by remember { mutableLongStateOf(0L) }
    if (progress == null) {
        LaunchedEffect(busy) {
            var last = 0L
            while (true) {
                androidx.compose.runtime.withFrameNanos { now ->
                    if (last != 0L) busy.tick(((now - last) / 1_000_000_000.0).toFloat().coerceAtMost(0.1f))
                    last = now
                    frame = now
                }
            }
        }
    }
    ComposeCanvas(modifier.fillMaxSize()) {
        @Suppress("UNUSED_EXPRESSION") frame // read to redraw every busy frame
        drawIntoCanvas { c ->
            VitaLoadingDialogRenderer.draw(c.nativeCanvas, size.width, size.height, assets, message, progress,
                if (progress == null) busy else null)
        }
    }
}
