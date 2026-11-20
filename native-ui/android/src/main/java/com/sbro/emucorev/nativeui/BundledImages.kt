package com.sbro.emucorev.nativeui

import java.io.File
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.text.TextPaint
import android.graphics.Typeface
import android.text.TextUtils
import org.json.JSONObject
import javax.xml.parsers.DocumentBuilderFactory
import org.xml.sax.InputSource
import org.xml.sax.SAXException
import org.w3c.dom.Element
import org.w3c.dom.Node
import java.io.ByteArrayInputStream
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

    fun loadLockImages(onHomeCatalogLoaded: (List<HomeCatalogEntry>) -> Unit = {}) {
        loadShellImages()
        loadHomeImages(onHomeCatalogLoaded)
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

    data class HomeCatalogEntry(
        val id: String,
        val title: String,
        val contentId: String,
        val iconImageId: Int,
        val labelImageId: Int,
        val liveAreaTemplate: String?,
        val liveAreaImages: List<String>,
    )

    data class HomeLiveAreaSnapshot(
        val titleId: String,
        val backgroundImageId: Int,
        val backgroundRect: FloatArray,
        val gateImageId: Int,
        val gateRect: FloatArray,
        val frameImageIds: IntArray,
        val frameRects: FloatArray,
        val textImageIds: IntArray,
        val textRects: FloatArray,
        val textColors: IntArray,
        val targetIds: Array<String>,
        val targetRects: FloatArray,
    )

    fun loadHomeImages(onLoaded: (List<HomeCatalogEntry>) -> Unit = {}): List<HomeCatalogEntry> {
        loadHomeLiveAreaChromeImages()
        val catalog = File(resources, "preview/home/catalog.json").readText()
        val catalogObject = JSONObject(catalog)
        require(catalogObject.getInt("version") == 1) { "Unsupported Home catalog version" }
        val games = catalogObject.getJSONArray("games")
        require(games.length() <= HOME_CATALOG_MAX_GAMES) { "Home catalog contains too many games" }
        val entries = ArrayList<HomeCatalogEntry>(games.length())
        val seenIds = HashSet<String>(games.length())
        val homeRoot = File(resources, "preview/home").canonicalFile
        val homePrefix = homeRoot.path + File.separator
        for (index in 0 until games.length()) {
            val game = games.getJSONObject(index)
            val id = game.getString("id")
            val title = game.getString("title")
            val contentId = game.getString("contentId")
            require(id.isNotBlank() && title.isNotBlank()) {
                "Home catalog contains an empty game field"
            }
            require(seenIds.add(id)) { "Home catalog contains duplicate id: $id" }
            val iconPath = File(homeRoot, game.getString("icon")).canonicalFile
            require(iconPath.path.startsWith(homePrefix) && iconPath.isFile) {
                "Invalid Home catalog icon path"
            }
            val iconImageId = HOME_ICON_IMAGE_BASE + index
            val labelImageId = HOME_LABEL_IMAGE_BASE + index
            val icon = iconPath.inputStream().use { requireNotNull(BitmapFactory.decodeStream(it)) }
            try {
                send(iconImageId, icon)
            } finally {
                icon.recycle()
            }
            val label = rasterHomeTitle(title)
            try {
                send(labelImageId, label)
            } finally {
                label.recycle()
            }
            entries += HomeCatalogEntry(
                id = id,
                title = title,
                contentId = contentId,
                iconImageId = iconImageId,
                labelImageId = labelImageId,
                liveAreaTemplate = game.optJSONObject("liveArea")?.optString("template")?.takeIf { it.isNotBlank() },
                liveAreaImages = game.optJSONObject("liveArea")?.optJSONArray("images")?.let { array ->
                    (0 until array.length()).map { array.getString(it) }
                } ?: emptyList(),
            )
        }
        // This is an optional extracted shell asset. When present, publish it
        // under the original PAF image ID used by the native Home renderer.
        File(resources, "images/applauncher_plugin/a952ef1e.png")
            .takeIf { it.isFile }
            ?.inputStream()?.use { stream ->
                val focusGlow = requireNotNull(BitmapFactory.decodeStream(stream)) {
                    "Invalid bundled Home focus image"
                }
                try {
                    send(HOME_FOCUS_GLOW_IMAGE, focusGlow)
                } finally {
                    focusGlow.recycle()
                }
            }
        onLoaded(entries)
        return entries
    }

    private fun loadHomeLiveAreaChromeImages() {
        listOf(
            0xca6a6db1.toInt() to "images/livearea_plugin/ca6a6db1.png",
        ).forEach { (imageId, path) ->
            val bitmap = File(resources, path).inputStream().use {
                requireNotNull(BitmapFactory.decodeStream(it)) { "Invalid bundled LiveArea chrome image: $path" }
            }
            try { send(imageId, bitmap) } finally { bitmap.recycle() }
        }
    }

    fun loadHomeLiveAreas(
        entries: List<HomeCatalogEntry>,
        onLoaded: (HomeLiveAreaSnapshot) -> Unit,
    ) {
        entries.forEachIndexed { gameIndex, entry ->
            val templateRelative = entry.liveAreaTemplate ?: return@forEachIndexed
            onLoaded(loadHomeLiveArea(entry, gameIndex, templateRelative))
        }
    }

    private fun loadHomeLiveArea(
        entry: HomeCatalogEntry,
        gameIndex: Int,
        templateRelative: String,
    ): HomeLiveAreaSnapshot {
        val homeRoot = File(resources, "preview/home").canonicalFile
        val homePrefix = homeRoot.path + File.separator
        fun asset(relative: String): File {
            val file = File(homeRoot, relative).canonicalFile
            require(file.path.startsWith(homePrefix) && file.isFile) {
                "Invalid LiveArea asset path: $relative"
            }
            return file
        }
        val template = asset(templateRelative)
        val allowedImages = entry.liveAreaImages.toHashSet()
        require(template.length() <= MAX_LIVEAREA_TEMPLATE_BYTES) {
            "LiveArea template is too large for ${entry.id}"
        }
        val templateBytes = template.readBytes()
        // Reject DTDs before invoking platform XML parsers. Some Android XML
        // implementations do not support the JAXP feature flags used on desktop.
        val markupForDtdCheck = templateBytes.filter { it != 0.toByte() }
            .toByteArray().toString(Charsets.ISO_8859_1)
        require(!DOCTYPE_PATTERN.containsMatchIn(markupForDtdCheck)) {
            "LiveArea template must not contain a DOCTYPE: ${entry.id}"
        }
        val documentFactory = DocumentBuilderFactory.newInstance().apply {
            isExpandEntityReferences = false
        }
        val builder = documentFactory.newDocumentBuilder().apply {
            setEntityResolver { _, _ ->
                throw SAXException("External entities are not allowed in LiveArea templates")
            }
        }
        val document = builder.parse(InputSource(ByteArrayInputStream(templateBytes)))
        val root = document.documentElement
        require(root.tagName == "livearea" && root.getAttribute("style") == "a3") {
            "Unsupported LiveArea template style for ${entry.id}"
        }
        var nextImageId = LIVEAREA_IMAGE_BASE + gameIndex * LIVEAREA_IMAGE_SLOTS_PER_GAME
        fun uploadAsset(relative: String): Pair<Int, Bitmap> {
            require(relative in allowedImages) { "LiveArea asset is not listed by the catalog: $relative" }
            require(nextImageId < LIVEAREA_IMAGE_BASE + (gameIndex + 1) * LIVEAREA_IMAGE_SLOTS_PER_GAME) {
                "Too many LiveArea images for ${entry.id}"
            }
            val imageId = nextImageId++
            val bitmap = asset(relative).inputStream().use {
                requireNotNull(BitmapFactory.decodeStream(it)) { "Invalid LiveArea image: $relative" }
            }
            try {
                send(imageId, bitmap)
            } catch (error: Exception) {
                bitmap.recycle()
                throw error
            }
            return imageId to bitmap
        }

        val backgroundRect = A3Layout.backgroundRect
        val backgroundOptions = root.children("livearea-background").firstOrNull()
            ?.children("image").orEmpty().map { element ->
                LiveAreaBackground(
                    file = element.directText(),
                    language = element.children("lang").firstOrNull()?.textContent?.trim().orEmpty(),
                    isDefault = element.getAttribute("default") == "on",
                )
            }.filter { it.file.isNotBlank() }
        val language = when (Locale.getDefault().language.lowercase(Locale.ROOT)) {
            "zh" -> "ch"
            else -> Locale.getDefault().language.lowercase(Locale.ROOT)
        }
        val selectedBackground = backgroundOptions.firstOrNull { it.language == language }
            ?: backgroundOptions.firstOrNull { it.isDefault }
            ?: backgroundOptions.firstOrNull()
        val backgroundPair = selectedBackground?.let { uploadAsset("livearea/${entry.id}/${it.file}") }
        val backgroundId = backgroundPair?.first ?: 0
        backgroundPair?.second?.recycle()

        val startupImage = root.children("gate").firstOrNull()
            ?.children("startup-image")?.firstOrNull()?.directText().orEmpty()
        val gatePair = startupImage.takeIf { it.isNotBlank() }
            ?.let { uploadAsset("livearea/${entry.id}/$it") }
        val gateId = gatePair?.first ?: 0
        gatePair?.second?.recycle()

        val frameImageIds = ArrayList<Int>()
        val frameRects = ArrayList<Float>()
        val textImageIds = ArrayList<Int>()
        val textRects = ArrayList<Float>()
        val textColors = ArrayList<Int>()
        val targetIds = ArrayList<String>()
        val targetRects = ArrayList<Float>()
        if (gateId != 0) {
            targetIds += entry.id
            targetRects += A3Layout.gateRect.toList()
        }

        for (frame in root.children("frame")) {
            val frameId = frame.getAttribute("id")
            val frameRect = A3Layout.frameRects[frameId] ?: continue
            val frameWidth = frameRect[2]
            val frameHeight = frameRect[3]
            for (liveItem in frame.children("liveitem")) {
                val imageElement = liveItem.children("image").firstOrNull()
                if (imageElement != null) {
                    val relative = imageElement.directText()
                    if (relative.isNotBlank()) {
                        val pair = uploadAsset("livearea/${entry.id}/$relative")
                        val bitmap = pair.second
                        val rect = A3Layout.imageRect(frameId, imageElement, bitmap.width, bitmap.height)
                        bitmap.recycle()
                        frameImageIds += pair.first
                        frameRects += rect.toList()
                    }
                }

                val textElement = liveItem.children("text").firstOrNull()
                if (textElement != null) {
                    val align = textElement.getAttribute("align").ifBlank { "center" }
                    val valign = textElement.getAttribute("valign").ifBlank { "center" }
                    val yOffset = textElement.getAttribute("y").toFloatOrNull() ?: 0f
                    for (str in textElement.children("str")) {
                        val content = str.textContent.orEmpty()
                        if (content.isBlank()) continue
                        val size = str.getAttribute("size").toFloatOrNull() ?: 19.2f
                        val color = str.getAttribute("color").let {
                            if (it.isBlank()) Color.WHITE else Color.parseColor(it)
                        }
                        val textBitmap = rasterLiveAreaText(content, size)
                        try {
                            val rect = A3Layout.textRect(
                                frameRect, textBitmap.width.toFloat(), textBitmap.height.toFloat(),
                                align, valign, yOffset,
                            )
                            val imageId = nextImageId++
                            require(imageId < LIVEAREA_IMAGE_BASE + (gameIndex + 1) * LIVEAREA_IMAGE_SLOTS_PER_GAME) {
                                "Too many LiveArea images for ${entry.id}"
                            }
                            send(imageId, textBitmap)
                            textImageIds += imageId
                            textRects += rect.toList()
                            textColors += color
                        } finally {
                            textBitmap.recycle()
                        }
                    }
                }

                val target = liveItem.children("target").firstOrNull()?.textContent.orEmpty()
                    .replace(Regex("\\s+"), "")
                if (target.isNotBlank()) {
                    targetIds += target
                    targetRects += frameRect.toList()
                }
            }
        }
        val snapshot = HomeLiveAreaSnapshot(
            titleId = entry.id,
            backgroundImageId = backgroundId,
            backgroundRect = if (backgroundId == 0) FloatArray(4) else backgroundRect,
            gateImageId = gateId,
            gateRect = if (gateId == 0) FloatArray(4) else A3Layout.gateRect,
            frameImageIds = frameImageIds.toIntArray(),
            frameRects = frameRects.toFloatArray(),
            textImageIds = textImageIds.toIntArray(),
            textRects = textRects.toFloatArray(),
            textColors = textColors.toIntArray(),
            targetIds = targetIds.toTypedArray(),
            targetRects = targetRects.toFloatArray(),
        )
        return snapshot
    }

    private fun rasterLiveAreaText(text: String, size: Float): Bitmap {
        val paint = TextPaint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.WHITE
            textSize = size
            typeface = Typeface.createFromFile(File(resources, "fonts/ltn0.otf"))
        }
        val width = ceil(paint.measureText(text)).toInt().coerceAtLeast(1)
        val height = ceil(paint.descent() - paint.ascent()).toInt().coerceAtLeast(1)
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        Canvas(bitmap).drawText(text, 0f, -paint.ascent(), paint)
        return bitmap
    }

    private data class LiveAreaBackground(val file: String, val language: String, val isDefault: Boolean)

    /** A3 geometry corroborated by ECV/gui/live_area.cpp (960x544 top-left space). */
    private object A3Layout {
        val backgroundRect = floatArrayOf(60f, 44f, 840f, 500f)
        val gateRect = floatArrayOf(340f, 149f, 280f, 158f)
        val frameRects = mapOf(
            "frame1" to floatArrayOf(60f, 130f, 260f, 200f),
            "frame2" to floatArrayOf(640f, 130f, 260f, 200f),
            "frame3" to floatArrayOf(60f, 330f, 260f, 210f),
            "frame4" to floatArrayOf(320f, 330f, 320f, 210f),
            "frame5" to floatArrayOf(640f, 330f, 260f, 210f),
        )

        fun imageRect(frameId: String, element: Element, sourceWidth: Int, sourceHeight: Int): FloatArray {
            val frame = requireNotNull(frameRects[frameId]) { "Unsupported a3 frame: $frameId" }
            val width = minOf(sourceWidth.toFloat(), frame[2])
            val height = minOf(sourceHeight.toFloat(), frame[3])
            val xOffset = element.getAttribute("x").toFloatOrNull() ?: 0f
            val yOffset = element.getAttribute("y").toFloatOrNull() ?: 0f
            val align = element.getAttribute("align").ifBlank { "center" }
            val valign = element.getAttribute("valign").ifBlank { "center" }
            var x = frame[0] + when (align) {
                "left" -> if (xOffset >= 0) 0f else (frame[2] - width) * .5f + xOffset
                "right" -> if (xOffset <= 0) frame[2] - width + xOffset else (frame[2] - width) * .5f + xOffset
                else -> (frame[2] - width) * .5f + xOffset
            }
            var y = frame[1] + when (valign) {
                "top" -> if (yOffset <= 0) 0f else (frame[3] - height) * .5f - yOffset
                "bottom" -> if (yOffset >= 0) frame[3] - height - yOffset else (frame[3] - height) * .5f - yOffset
                else -> (frame[3] - height) * .5f - yOffset
            }
            x = x.coerceIn(frame[0], frame[0] + frame[2] - width)
            y = y.coerceIn(frame[1], frame[1] + frame[3] - height)
            return floatArrayOf(x, y, width, height)
        }

        fun textRect(
            frame: FloatArray,
            width: Float,
            height: Float,
            align: String,
            valign: String,
            yOffset: Float,
        ): FloatArray {
            val x = frame[0] + when (align) {
                "left" -> 0f
                "right" -> frame[2] - width
                else -> (frame[2] - width) * .5f
            }
            val y = frame[1] + when (valign) {
                "top" -> 0f
                "bottom" -> frame[3] - height
                else -> (frame[3] - height) * .5f
            } - yOffset
            return floatArrayOf(x, y, width, height)
        }
    }

    private fun Element.children(tag: String): List<Element> {
        val result = ArrayList<Element>()
        var child = firstChild
        while (child != null) {
            if (child.nodeType == Node.ELEMENT_NODE) {
                val element = child as Element
                if (element.tagName == tag) result += element
            }
            child = child.nextSibling
        }
        return result
    }

    private fun Element.directText(): String {
        var child = firstChild
        while (child != null) {
            if (child.nodeType == Node.TEXT_NODE || child.nodeType == Node.CDATA_SECTION_NODE) {
                val text = child.textContent.trim()
                if (text.isNotEmpty()) return text
            }
            child = child.nextSibling
        }
        return ""
    }

    private fun rasterHomeTitle(title: String): Bitmap {
        val paint = TextPaint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.WHITE
            textSize = 18f // AppIcon2d uses AppIconBase TextObj font_size 18 (PAF constructor 0x810d8906).
            typeface = Typeface.createFromFile(File(resources, "fonts/ltn0.otf"))
        }
        val words = title.split(Regex("\\s+")).filter { it.isNotEmpty() }
        val lines = ArrayList<String>(2)
        var line = ""
        for (word in words) {
            val candidate = if (line.isEmpty()) word else "$line $word"
            if (line.isNotEmpty() && paint.measureText(candidate) > HOME_LABEL_MAX_WIDTH) {
                lines += line
                line = word
            } else {
                line = candidate
            }
        }
        if (line.isNotEmpty()) lines += line
        if (lines.size > 2) lines[1] = lines.drop(1).joinToString(" ")
        while (lines.size > 2) lines.removeAt(lines.lastIndex)
        val renderedLines = lines.map {
            TextUtils.ellipsize(it, paint, HOME_LABEL_MAX_WIDTH, TextUtils.TruncateAt.END).toString()
        }
        val width = (renderedLines.maxOfOrNull { paint.measureText(it) } ?: 1f)
            .toInt().coerceIn(1, HOME_LABEL_MAX_WIDTH.toInt())
        val bitmap = Bitmap.createBitmap(width, HOME_LABEL_HEIGHT, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        renderedLines.forEachIndexed { index, text ->
            val x = (width - paint.measureText(text)) * .5f
            canvas.drawText(text, x, HOME_LABEL_FIRST_BASELINE + index * HOME_LABEL_LINE_PITCH, paint)
        }
        return bitmap
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
        val busy = File(resources, "images/topmenu_plugin/754463d1.png").inputStream().use {
            requireNotNull(BitmapFactory.decodeStream(it)) { "Invalid bundled busy image" }
        }
        try { loadBusyImage(busy) } finally { busy.recycle() }
    }

    /** Publish an early standalone busy image before the full offline pack is extracted. */
    fun loadBusyImage(busy: Bitmap) {
        send(0x754463d1.toInt(), busy)
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
        const val HOME_ICON_IMAGE_BASE = 1000
        const val HOME_LABEL_IMAGE_BASE = 2000
        const val HOME_FOCUS_GLOW_IMAGE = 0xa952ef1e.toInt()
        private const val LIVEAREA_IMAGE_BASE = 3000
        private const val LIVEAREA_IMAGE_SLOTS_PER_GAME = 64
        private const val HOME_CATALOG_MAX_GAMES = 512
        private const val MAX_LIVEAREA_TEMPLATE_BYTES = 1024L * 1024L
        private val DOCTYPE_PATTERN = Regex("<!\\s*DOCTYPE\\b", RegexOption.IGNORE_CASE)
        private const val HOME_LABEL_MAX_WIDTH = 180f
        private const val HOME_LABEL_HEIGHT = 64
        private const val HOME_LABEL_FIRST_BASELINE = 22f
        private const val HOME_LABEL_LINE_PITCH = 40
    }
}
