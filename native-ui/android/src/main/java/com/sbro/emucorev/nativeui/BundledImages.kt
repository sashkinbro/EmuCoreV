package com.sbro.emucorev.nativeui

import android.content.res.AssetManager
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.util.Locale
import kotlin.math.ceil

/** Decode packaged images and rasterize changed text; the C++ scene owns layout. */
class BundledImages(
    private val assets: AssetManager,
    private val upload: (Int, Int, Int, ByteArray) -> Unit,
) {
    private val clockPaint by lazy {
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.WHITE
            textSize = 22f
            typeface = Typeface.createFromAsset(assets, "fonts/ltn4.otf")
        }
    }
    private var previousClock: String? = null

    fun loadIndicatorImages() {
        // Resource hashes from the reviewed indicator document, not guessed names.
        listOf("ca76be07", "78600544", "4a8ae255", "9e9cfcab", "f85f0b80",
            "56e72f16", "a5aafd9b", "32465c57", "00ea236e", "89dc8410",
            "2278fb2a", "ee958f99", "a13376bc").forEach { id ->
            val bitmap = assets.open("images/indicator_plugin/$id.png").use {
                requireNotNull(BitmapFactory.decodeStream(it)) { "Invalid bundled image: $id" }
            }
            try { send(id.toLong(16).toInt(), bitmap) } finally { bitmap.recycle() }
        }
        upload(0, 1, 1, byteArrayOf(-1, -1, -1, -1))
    }

    fun updateClock(status: DeviceStatusSource.Snapshot) {
        val pattern = if (status.use24HourClock) "HH:mm" else "h:mm a"
        val text = DateTimeFormatter.ofPattern(pattern, Locale.getDefault())
            .withZone(ZoneId.of(status.timeZoneId))
            .format(Instant.ofEpochSecond(status.epochMinute * 60))
        if (text == previousClock) return
        val paint = clockPaint
        val width = ceil(paint.measureText(text)).toInt() + 4
        val bitmap = Bitmap.createBitmap(width, 32, Bitmap.Config.ARGB_8888)
        try {
            val baseline = (32f - paint.ascent() - paint.descent()) * .5f
            Canvas(bitmap).drawText(text, 2f, baseline, paint)
            send(CLOCK_IMAGE, bitmap)
            previousClock = text
        } finally { bitmap.recycle() }
    }

    private fun send(id: Int, bitmap: Bitmap) {
        // getPixels returns non-premultiplied ARGB, independent of native bitmap
        // byte order. The GLES renderer consumes explicit straight-alpha RGBA.
        val pixels = IntArray(bitmap.width * bitmap.height)
        bitmap.getPixels(pixels, 0, bitmap.width, 0, 0, bitmap.width, bitmap.height)
        val rgba = ByteArray(pixels.size * 4)
        pixels.forEachIndexed { index, argb ->
            val offset = index * 4
            rgba[offset] = (argb ushr 16).toByte()
            rgba[offset + 1] = (argb ushr 8).toByte()
            rgba[offset + 2] = argb.toByte()
            rgba[offset + 3] = (argb ushr 24).toByte()
        }
        upload(id, bitmap.width, bitmap.height, rgba)
    }

    companion object { const val CLOCK_IMAGE = 1 }
}
