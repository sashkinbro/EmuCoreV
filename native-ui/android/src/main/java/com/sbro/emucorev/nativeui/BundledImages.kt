package com.sbro.emucorev.nativeui

import java.io.File
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
    private val resources: File,
    private val upload: (Int, Int, Int, ByteArray) -> Unit,
) {
    /** Text layout from power_manage_plugin RCO template f5228777 (group de1f6be8). */
    private object LockRcoLayout {
        const val canvasWidth = 960f
        const val canvasHeight = 544f
        const val groupX = -430f
        const val groupY = -222f
        const val dateX = 10f
        const val dateY = 108f
        const val hourX = 160f
        const val minuteX = 380f
        const val separatorX = -52f
        const val amPmX = 390f
        const val textY = 0f

        // PAF text layout uses a centered widget origin and a Y-up coordinate system.
        fun screenX(localX: Float) = canvasWidth * .5f + groupX + localX
        fun screenBaseline(localY: Float) = canvasHeight * .5f - (groupY + localY)
    }

    private val clockPaint by lazy {
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.WHITE
            textSize = 22f
            typeface = Typeface.createFromFile(File(resources, "fonts/ltn4.otf"))
        }
    }
    private val lockClockPaint by lazy {
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = 0xfff2f2f2.toInt()
            textSize = 120f // power_manage_plugin RCO textobj 6232
            // The reference lock text is SCE Rodin Cattleya LATIN Regular (ltn0),
            // not its Bold sibling ltn4 used by the shell status clock.
            typeface = Typeface.createFromFile(File(resources, "fonts/ltn0.otf"))
            setShadowLayer(1f, 0f, 1f, Color.argb(128, 0, 0, 0))
        }
    }
    private val lockAmPmPaint by lazy {
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = 0xfff2f2f2.toInt()
            textSize = 52f // RCO textobj 6552, bold=true
            typeface = Typeface.createFromFile(File(resources, "fonts/ltn4.otf"))
            setShadowLayer(1f, 0f, 1f, Color.argb(128, 0, 0, 0))
        }
    }
    private val lockDatePaint by lazy {
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = 0xfff2f2f2.toInt()
            textSize = 32f // RCO textobj 6064
            typeface = Typeface.createFromFile(File(resources, "fonts/ltn0.otf"))
            setShadowLayer(1f, 0f, 1f, Color.argb(128, 0, 0, 0))
        }
    }
    private val lockSeparatorPaint by lazy {
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = 0xfff2f2f2.toInt()
            textSize = 120f // RCO textobj 6736
            typeface = Typeface.createFromFile(File(resources, "fonts/ltn0.otf"))
            setShadowLayer(1f, 0f, 1f, Color.argb(128, 0, 0, 0))
        }
    }
    private var previousClock: String? = null
    private var rawKeylock: Bitmap? = null
    private var previousLockText: String? = null

    fun loadShellImages() {
        // Resource hashes from the reviewed indicator document, not guessed names.
        listOf("ca76be07", "78600544", "4a8ae255", "9e9cfcab", "f85f0b80",
            "56e72f16", "a5aafd9b", "32465c57", "00ea236e", "89dc8410",
            "2278fb2a", "ee958f99", "a13376bc").forEach { id ->
            val bitmap = File(resources, "images/indicator_plugin/$id.png").inputStream().use {
                requireNotNull(BitmapFactory.decodeStream(it)) { "Invalid bundled image: $id" }
            }
            try { send(id.toLong(16).toInt(), bitmap) } finally { bitmap.recycle() }
        }
        loadBusyImage()
        upload(0, 1, 1, byteArrayOf(-1, -1, -1, -1))
    }

    fun loadLockImages() {
        loadShellImages()
        // Lock wallpaper, original corner texture and the exponential lookup.
        listOf(
            CORNER_IMAGE to "effects/page_turn/textures/page_corner/mip-0.png",
            EXP_MINUS_IMAGE to "effects/lookups/exp-minus.png"
        ).forEach { (id, path) ->
            val bitmap = File(resources, path).inputStream().use {
                requireNotNull(BitmapFactory.decodeStream(it)) { "Invalid bundled lock image: $path" }
            }
            try { send(id, bitmap) } finally { bitmap.recycle() }
        }
        val keylockFile = File(resources, "effects/keylock.png")
        val base = keylockFile.inputStream().use {
            requireNotNull(BitmapFactory.decodeStream(it)) { "Invalid bundled keylock wallpaper" }
        }
        releaseLockWallpaper()
        rawKeylock = base
        send(RAW_KEYLOCK_IMAGE, base)
    }

    fun updateLockClock(status: DeviceStatusSource.Snapshot) {
        val instant = Instant.ofEpochSecond(status.epochMinute * 60)
        val zone = ZoneId.of(status.timeZoneId)
        val locale = Locale.getDefault()
        val hourText = DateTimeFormatter.ofPattern(if (status.use24HourClock) "HH" else "h", locale)
            .withZone(zone).format(instant)
        val minuteText = DateTimeFormatter.ofPattern("mm", locale).withZone(zone).format(instant)
        val amPmText = if (!status.use24HourClock) {
            DateTimeFormatter.ofPattern("a", locale).withZone(zone).format(instant)
        } else ""
        // The source lock date is a full month, day, and parenthesized weekday.
        // Keep the device locale for the translated names while preserving that layout.
        val dateText = DateTimeFormatter.ofPattern("MMMM d (EEEE)", locale)
            .withZone(zone).format(instant)

        val combinedKey = "$hourText|$minuteText|$amPmText|$dateText"
        if (combinedKey == previousLockText) return
        val base = rawKeylock ?: return
        val composited = base.copy(Bitmap.Config.ARGB_8888, true)
        val canvas = Canvas(composited)
        val dateX = LockRcoLayout.screenX(LockRcoLayout.dateX)
        val dateBaseline = LockRcoLayout.screenBaseline(LockRcoLayout.dateY)
        canvas.drawText(dateText, dateX, dateBaseline, lockDatePaint)

        // These are the source widget anchors: hour at 160, minute at 380, with
        // the minute field right-aligned. The nested separator sits 52 units left.
        val clockBaseline = LockRcoLayout.screenBaseline(LockRcoLayout.textY)
        val hourRight = LockRcoLayout.screenX(LockRcoLayout.hourX)
        val minuteRight = LockRcoLayout.screenX(LockRcoLayout.minuteX)
        val minuteLeftInGroup = LockRcoLayout.minuteX - lockClockPaint.measureText(minuteText)
        canvas.drawText(hourText, hourRight - lockClockPaint.measureText(hourText),
            clockBaseline, lockClockPaint)
        canvas.drawText(":", LockRcoLayout.screenX(minuteLeftInGroup + LockRcoLayout.separatorX),
            clockBaseline, lockSeparatorPaint)
        canvas.drawText(minuteText, minuteRight - lockClockPaint.measureText(minuteText),
            clockBaseline, lockClockPaint)
        if (amPmText.isNotEmpty()) {
            canvas.drawText(amPmText, LockRcoLayout.screenX(LockRcoLayout.amPmX),
                clockBaseline, lockAmPmPaint)
        }
        try {
            send(KEYLOCK_IMAGE, composited)
            previousLockText = combinedKey
        } finally {
            composited.recycle()
        }
    }

    fun release() {
        releaseLockWallpaper()
    }

    private fun releaseLockWallpaper() {
        rawKeylock?.let { if (!it.isRecycled) it.recycle() }
        rawKeylock = null
        previousLockText = null
    }

    fun loadBusyImage() {
        val busy = File(resources, "images/paf-common-resource/ba195a37.png").inputStream().use {
            requireNotNull(BitmapFactory.decodeStream(it)) { "Invalid bundled busy image" }
        }
        try { send(0xba195a37.toInt(), busy) } finally { busy.recycle() }
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

    companion object {
        const val CLOCK_IMAGE = 1
        const val KEYLOCK_IMAGE = 100
        const val CORNER_IMAGE = 101
        const val EXP_MINUS_IMAGE = 102
        const val RAW_KEYLOCK_IMAGE = 103
    }
}
