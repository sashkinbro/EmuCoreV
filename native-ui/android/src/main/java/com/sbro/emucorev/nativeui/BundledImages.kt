package com.sbro.emucorev.nativeui

import java.io.File
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.PorterDuff
import android.graphics.PorterDuffXfermode
import android.graphics.Rect
import android.text.TextPaint
import android.text.Layout
import android.text.StaticLayout
import android.graphics.Typeface
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
    private val clockAmPmPaint by lazy {
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.WHITE
            textSize = 15f
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

    /** Uploads one pack texture under its resource id (shell applications). */
    fun loadTexture(id: Int, relativePath: String) {
        if (File(resources, relativePath).isFile) loadPngResource(id, relativePath)
    }

    /** Uploads a host raster (shell application rows and headers). */
    fun uploadBitmap(id: Int, bitmap: Bitmap) = send(id, bitmap)

    private fun loadPngResource(id: Int, relativePath: String) {
        val bitmap = File(resources, relativePath).inputStream().use {
            requireNotNull(BitmapFactory.decodeStream(it)) { "Invalid bundled image: $relativePath" }
        }
        try { send(id, bitmap) } finally { bitmap.recycle() }
    }

    fun loadShellImages() {
        // Resource hashes from the reviewed indicator document, not guessed names.
        listOf("ca76be07", "78600544", "4a8ae255", "9e9cfcab", "f85f0b80",
            "56e72f16", "a5aafd9b", "32465c57", "00ea236e", "89dc8410",
            "2278fb2a", "ee958f99", "a13376bc",
            "bef34d7f", "6821404a", "94de0b89", "fbc7297e",
            "e062c41f", "fede49bf", "d29cb815", "fec9dfd8").forEach { id ->
            loadPngResource(id.toLong(16).toInt(), "images/indicator_plugin/$id.png")
        }
        loadPngResource(STATUS_HOME_ICON, "images/applauncher_plugin/56aebd5f.png")
        // Application corner button (paf-common corner_button style 35ca8ee7).
        listOf("fb2979d5", "352ffde1", "c903feec").forEach { id ->
            loadTexture(id.toLong(16).toInt(), "images/paf-common-resource/$id.png")
        }
        listOf(
            TOPMENU_ARROW_LEFT to "images/topmenu_plugin/8ca9d181.png",
            TOPMENU_ARROW_RIGHT to "images/topmenu_plugin/168e9e6b.png",
        ).forEach { (id, rel) ->
            if (File(resources, rel).isFile) loadPngResource(id, rel)
        }
        loadBusyImage()
        upload(0, 1, 1, byteArrayOf(-1, -1, -1, -1))
    }

    fun loadLockImages(
        catalog: List<HomeCatalogSource>,
        onHomeCatalogLoaded: (List<HomeCatalogEntry>) -> Unit = {},
    ) {
        loadShellImages()
        loadHomeImages(catalog, onHomeCatalogLoaded)
        // Lock wallpaper, original corner texture, inner peel background, and exponential lookup.
        listOf(
            CORNER_IMAGE to "effects/page_turn/textures/page_corner/mip-0.png",
            EXP_MINUS_IMAGE to "effects/lookups/exp-minus.png",
            LOCK_EMPTY_BG_IMAGE to "images/power_manage_plugin/ca707736.png",
        ).forEach { (id, path) -> loadPngResource(id, path) }
        val keylockFile = File(resources, "effects/keylock.png")
        val base = keylockFile.inputStream().use {
            requireNotNull(BitmapFactory.decodeStream(it)) { "Invalid bundled keylock wallpaper" }
        }
        releaseLockWallpaper()
        rawKeylock = base
        send(RAW_KEYLOCK_IMAGE, base)
    }

    /**
     * One Home bubble supplied by the host: an installed game (its sce_sys
     * icon and LiveArea directory) or a system application.
     */
    data class HomeCatalogSource(
        val id: String,
        val title: String,
        val contentId: String,
        val icon: File,
        /** Directory holding template.xml and the images it references. */
        val liveAreaRoot: File?,
    )

    data class HomeCatalogEntry(
        val id: String,
        val title: String,
        val contentId: String,
        val iconImageId: Int,
        val labelImageId: Int,
        val liveAreaRoot: File?,
    )

    data class HomeLiveAreaSnapshot(
        val titleId: String,
        val backgroundImageId: Int,
        val backgroundRect: FloatArray,
        val gateImageId: Int,
        val gateRect: FloatArray,
        val gateStartLabelImageId: Int,
        val gateControlImageId: Int,
        val frameImageIds: IntArray,
        val frameRects: FloatArray,
        val textImageIds: IntArray,
        val textRects: FloatArray,
        val textColors: IntArray,
        val targetIds: Array<String>,
        val targetRects: FloatArray,
    )

    fun loadHomeImages(
        catalog: List<HomeCatalogSource>,
        onLoaded: (List<HomeCatalogEntry>) -> Unit = {},
    ): List<HomeCatalogEntry> {
        loadHomeLiveAreaChromeImages()
        require(catalog.size <= HOME_CATALOG_MAX_GAMES) { "Home catalog contains too many games" }
        val entries = ArrayList<HomeCatalogEntry>(catalog.size)
        val seenIds = HashSet<String>(catalog.size)
        val chipMask = File(resources, "images/indicator_plugin/fbc7297e.png")
            .takeIf { it.isFile }
            ?.inputStream()?.use { BitmapFactory.decodeStream(it) }
        try {
            for ((index, game) in catalog.withIndex()) {
                require(game.id.isNotBlank() && game.title.isNotBlank()) {
                    "Home catalog contains an empty game field"
                }
                if (!seenIds.add(game.id)) continue
                val iconImageId = HOME_ICON_IMAGE_BASE + index
                val labelImageId = HOME_LABEL_IMAGE_BASE + index
                val icon = runCatching {
                    game.icon.inputStream().use { BitmapFactory.decodeStream(it) }
                }.getOrNull() ?: continue
                try {
                    send(iconImageId, icon)
                    val chip = rasterCircularChip(icon, chipMask)
                    try {
                        send(HOME_CHIP_IMAGE_BASE + index, chip)
                    } finally {
                        chip.recycle()
                    }
                } finally {
                    icon.recycle()
                }
                val label = rasterHomeTitle(game.title)
                try {
                    send(labelImageId, label)
                } finally {
                    label.recycle()
                }
                entries += HomeCatalogEntry(
                    id = game.id,
                    title = game.title,
                    contentId = game.contentId,
                    iconImageId = iconImageId,
                    labelImageId = labelImageId,
                    liveAreaRoot = game.liveAreaRoot,
                )
            }
        } finally {
            chipMask?.recycle()
        }
        // Original Home shell RCO chrome: focus halo quadrant, page dots, edit/folder chrome, Home house.
        listOf(
            HOME_FOCUS_GLOW_IMAGE to "images/applauncher_plugin/a952ef1e.png",
            HOME_FOCUS_RING_IMAGE to "images/applauncher_plugin/cf115394.png",
            HOME_PAGE_DOT_ACTIVE to "images/applauncher_plugin/21ad6d05.png",
            HOME_PAGE_DOT_INACTIVE to "images/applauncher_plugin/266627f6.png",
            STATUS_HOME_ICON to "images/applauncher_plugin/56aebd5f.png",
            EDIT_NEW_PAGE_ICON to "images/applauncher_plugin/23ab06c5.png",
            EDIT_WALLPAPER_ICON to "images/applauncher_plugin/02024c2c.png",
            FOLDER_PANEL_BG to "images/applauncher_plugin/d8cdfae1.png",
            FOLDER_ICON to "images/applauncher_plugin/e35fe13a.png",
            FOLDER_TITLE_PILL to "images/applauncher_plugin/f1455f7e.png",
        ).forEach { (id, relPath) ->
            if (File(resources, relPath).isFile) loadPngResource(id, relPath)
        }
        loadHomeEditLabels()
        onLoaded(entries)
        return entries
    }

    object Fixture {
        /** Parses a development catalog (preview fixture) into host sources. */
        fun fixtureCatalog(homeRoot: File): List<HomeCatalogSource> {
            val root = homeRoot.canonicalFile
            val prefix = root.path + File.separator
            fun inside(relative: String): File = File(root, relative).canonicalFile.also {
                require(it.path.startsWith(prefix)) { "Invalid fixture path: $relative" }
            }
            val catalog = JSONObject(File(root, "catalog.json").readText())
            require(catalog.getInt("version") == 1) { "Unsupported Home catalog version" }
            val games = catalog.getJSONArray("games")
            return (0 until games.length()).map { index ->
                val game = games.getJSONObject(index)
                HomeCatalogSource(
                    id = game.getString("id"),
                    title = game.getString("title"),
                    contentId = game.optString("contentId"),
                    icon = inside(game.getString("icon")),
                    liveAreaRoot = game.optJSONObject("liveArea")?.optString("template")
                        ?.takeIf { it.isNotBlank() }?.let { inside(it).parentFile },
                )
            }
        }
    }

    private fun loadHomeLiveAreaChromeImages() {
        listOf(
            0xca6a6db1.toInt() to "images/livearea_plugin/ca6a6db1.png",
            0x1e6a33de to "images/livearea_plugin/1e6a33de.png",
            LIVEAREA_GATE_GLOW_IMAGE to "images/livearea_plugin/d5dd35e7.png",
            EDIT_BACK_ICON to "images/livearea_plugin/2cac72e8.png",
            EXP_MINUS_IMAGE to "effects/lookups/exp-minus.png",
        ).forEach { (imageId, path) ->
            if (File(resources, path).isFile) loadPngResource(imageId, path)
        }
    }

    /**
     * Decodes one LiveArea on demand (opening a bubble), so a large library
     * never keeps every background image resident.
     */
    fun loadHomeLiveArea(
        entry: HomeCatalogEntry,
        gameIndex: Int,
        gateStartLabel: String,
    ): HomeLiveAreaSnapshot? {
        val liveAreaRoot = entry.liveAreaRoot?.canonicalFile ?: return null
        val rootPrefix = liveAreaRoot.path + File.separator
        fun asset(relative: String): File {
            val file = File(liveAreaRoot, relative).canonicalFile
            require(file.path.startsWith(rootPrefix) && file.isFile) {
                "Invalid LiveArea asset path: $relative"
            }
            return file
        }
        val template = File(liveAreaRoot, "template.xml").takeIf { it.isFile } ?: return null
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
        require(root.tagName == "livearea") {
            "Unsupported LiveArea root tag for ${entry.id}"
        }
        val styleSpec = LiveAreaStyles.resolve(root.getAttribute("style"))
        var nextImageId = LIVEAREA_IMAGE_BASE + gameIndex * LIVEAREA_IMAGE_SLOTS_PER_GAME
        fun uploadAsset(relative: String): Pair<Int, Bitmap> {
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

        val backgroundRect = styleSpec.backgroundRect
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
        val backgroundPair = selectedBackground?.let { uploadAsset(it.file) }
        val backgroundId = backgroundPair?.first ?: 0
        backgroundPair?.second?.recycle()

        val startupImage = root.children("gate").firstOrNull()
            ?.children("startup-image")?.firstOrNull()?.directText().orEmpty()
        val gatePair = startupImage.takeIf { it.isNotBlank() }
            ?.let { uploadAsset(it) }
        val gateId = gatePair?.first ?: 0
        gatePair?.second?.recycle()

        var gateStartLabelImageId = 0
        if (gateStartLabel.isNotBlank()) {
            require(nextImageId < LIVEAREA_IMAGE_BASE + (gameIndex + 1) * LIVEAREA_IMAGE_SLOTS_PER_GAME) {
                "Too many LiveArea images for ${entry.id}"
            }
            val label = rasterGateStartLabel(gateStartLabel)
            try {
                gateStartLabelImageId = nextImageId++
                send(gateStartLabelImageId, label)
            } finally {
                label.recycle()
            }
        }

        val frameImageIds = ArrayList<Int>()
        val frameRects = ArrayList<Float>()
        val textImageIds = ArrayList<Int>()
        val textRects = ArrayList<Float>()
        val textColors = ArrayList<Int>()
        val targetIds = ArrayList<String>()
        val targetRects = ArrayList<Float>()
        targetIds += entry.id
        targetRects += styleSpec.gateRect.toList()

        for (frame in root.children("frame")) {
            val frameId = frame.getAttribute("id")
            val frameRect = styleSpec.frameRects[frameId] ?: continue
            for (liveItem in localizedLiveItems(frame.children("liveitem"))) {
                var itemImageRect: FloatArray? = null
                val imageElement = liveItem.children("image").firstOrNull()
                if (imageElement != null) {
                    val relative = imageElement.directText()
                    if (relative.isNotBlank()) {
                        val pair = uploadAsset(relative)
                        val bitmap = pair.second
                        val rect = styleSpec.imageRect(frameId, imageElement, bitmap.width, bitmap.height)
                        itemImageRect = rect
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
                            val image = itemImageRect
                            val rect = if (textElement.getAttribute("origin") == "image" && image != null) {
                                // Text placed against its item's image (e.g. outside-right).
                                val w = textBitmap.width.toFloat()
                                val h = textBitmap.height.toFloat()
                                val x = when (align) {
                                    "outside-right" -> image[0] + image[2]
                                    "outside-left" -> image[0] - w
                                    "left" -> image[0]
                                    "right" -> image[0] + image[2] - w
                                    else -> image[0] + (image[2] - w) * .5f
                                }
                                val y = when (valign) {
                                    "outside-top" -> image[1] - h
                                    "outside-bottom" -> image[1] + image[3]
                                    "top" -> image[1]
                                    "bottom" -> image[1] + image[3] - h
                                    else -> image[1] + (image[3] - h) * .5f
                                } - yOffset
                                floatArrayOf(x, y, w, h)
                            } else styleSpec.textRect(
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
            gateRect = if (gateId == 0) FloatArray(4) else styleSpec.gateRect,
            gateStartLabelImageId = gateStartLabelImageId,
            gateControlImageId = if (gateId == 0) 0 else LIVEAREA_START_CONTROL_IMAGE,
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

    /**
     * Live items may be duplicated per <lang>: keep the one for the system
     * language (LiveArea codes; zh = simplified, ch = traditional, pt-br),
     * else English, else the first; items without <lang> always stay.
     */
    private fun localizedLiveItems(items: List<Element>): List<Element> {
        val tagged = items.filter { it.children("lang").isNotEmpty() }
        if (tagged.isEmpty()) return items
        val locale = Locale.getDefault()
        val code = when (val language = locale.language.lowercase(Locale.ROOT)) {
            "zh" -> if (locale.country in setOf("TW", "HK", "MO") || locale.script == "Hant") "ch" else "zh"
            "pt" -> if (locale.country == "BR") "pt-br" else "pt"
            "nb", "nn" -> "no"
            "in" -> "id"
            else -> language
        }
        fun lang(item: Element) = item.children("lang").first().textContent.trim().lowercase(Locale.ROOT)
        val chosen = tagged.firstOrNull { lang(it) == code }
            ?: tagged.firstOrNull { lang(it) == code.substringBefore('-') }
            ?: tagged.firstOrNull { lang(it) == "en" }
            ?: tagged.first()
        return items.filter { it.children("lang").isEmpty() } + chosen
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

    private fun rasterGateStartLabel(text: String): Bitmap {
        val paint = TextPaint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.WHITE
            textSize = 24f
            typeface = Typeface.createFromFile(File(resources, "fonts/ltn4.otf"))
        }
        // The original control is 136 px wide; longer translations widen it
        // (16 px side padding inside the 3x1 slices) up to 260 px, beyond which
        // the label is condensed to fit rather than overflowing the capsule.
        val textWidth = paint.measureText(text)
        val width = ceil(textWidth + 32f).toInt().coerceIn(136, 260)
        if (textWidth > width - 32f) paint.textScaleX = (width - 32f) / textWidth
        val bitmap = Bitmap.createBitmap(width, 36, Bitmap.Config.ARGB_8888)
        val baseline = (36f - (paint.descent() + paint.ascent())) * 0.5f
        val x = (width - paint.measureText(text)) * 0.5f
        Canvas(bitmap).drawText(text, x, baseline, paint)
        return bitmap
    }

    private data class LiveAreaBackground(val file: String, val language: String, val isDefault: Boolean)

    private fun rasterCircularChip(icon: Bitmap, mask: Bitmap?): Bitmap {
        val size = 48
        val out = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(out)
        val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
        canvas.drawBitmap(icon, null, Rect(0, 0, size, size), paint)
        if (mask != null) {
            paint.xfermode = PorterDuffXfermode(PorterDuff.Mode.DST_IN)
            canvas.drawBitmap(mask, null, Rect(0, 0, size, size), paint)
        }
        return out
    }

    private fun rasterSingleLineLabel(text: String, textSizePx: Float, widthPx: Int, heightPx: Int): Bitmap {
        val paint = TextPaint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.WHITE
            textSize = textSizePx
            typeface = Typeface.createFromFile(File(resources, "fonts/ltn4.otf"))
            setShadowLayer(2.0f, 0f, 1.2f, 0xCC000000.toInt())
        }
        val bitmap = Bitmap.createBitmap(widthPx, heightPx, Bitmap.Config.ARGB_8888)
        val baseline = (heightPx.toFloat() - (paint.descent() + paint.ascent())) * 0.5f
        val x = ((widthPx.toFloat() - paint.measureText(text)) * 0.5f).coerceAtLeast(2f)
        Canvas(bitmap).drawText(text, x, baseline, paint)
        return bitmap
    }

    /**
     * Notification balloon message (indicator_plugin string 187da0f4), matched
     * against the emulated SceShell frame: ltn0 29 px, grey 0x50 on the 0xE3
     * balloon, baseline 32 px into a 44 px tall image.
     */
    fun loadNotificationText(text: String) {
        val paint = TextPaint(Paint.ANTI_ALIAS_FLAG).apply {
            color = 0xFF505050.toInt()
            textSize = 29f
            typeface = homeTitleTypeface
            setShadowLayer(1.2f, 0f, 1f, 0x40000000)
        }
        val width = ceil(paint.measureText(text)).toInt() + 8
        val bitmap = Bitmap.createBitmap(width.coerceIn(8, 900), 44, Bitmap.Config.ARGB_8888)
        try {
            Canvas(bitmap).drawText(text, 4f, 32f, paint)
            send(NOTIFICATION_TEXT_IMAGE, bitmap)
        } finally { bitmap.recycle() }
    }

    private fun loadHomeEditLabels() {
        val isUk = Locale.getDefault().language.lowercase(Locale.ROOT) == "uk"
        val folderTitle = rasterHomeTitle(if (isUk) "Папка" else "Folder")
        try { send(FOLDER_LABEL_IMAGE, folderTitle) } finally { folderTitle.recycle() }

        val createFolder = rasterSingleLineLabel(
            if (isUk) "Створити папку" else "Create Folder", 18f, 188, 42)
        try { send(EDIT_CREATE_FOLDER_LABEL, createFolder) } finally { createFolder.recycle() }

        val ungroup = rasterSingleLineLabel(
            if (isUk) "Розгрупувати" else "Ungroup", 18f, 188, 42)
        try { send(EDIT_UNGROUP_LABEL, ungroup) } finally { ungroup.recycle() }

        val done = rasterSingleLineLabel(
            if (isUk) "Готово" else "Done", 18f, 128, 36)
        try { send(EDIT_DONE_LABEL, done) } finally { done.recycle() }
    }

    private class LiveAreaStyleSpec(
        val gateRect: FloatArray,
        val frameRects: Map<String, FloatArray>,
    ) {
        val backgroundRect = floatArrayOf(60f, 44f, 840f, 500f)

        fun imageRect(frameId: String, element: Element, sourceWidth: Int, sourceHeight: Int): FloatArray {
            val frame = requireNotNull(frameRects[frameId]) { "Unsupported frame: $frameId" }
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

    /** Reconstructed LiveArea A1..A5 / Browser / CM / PSMobile coordinate specs from live_area.cpp. */
    private object LiveAreaStyles {
        private fun rect(posX: Float, posY: Float, w: Float, h: Float) =
            floatArrayOf(960f - posX, 544f - posY, w, h)

        private val specs = mapOf(
            "a1" to LiveAreaStyleSpec(
                gateRect = rect(620f, 361f, 280f, 158f),
                frameRects = mapOf(
                    "frame1" to rect(900f, 414f, 260f, 260f),
                    "frame2" to rect(320f, 414f, 260f, 260f),
                    "frame3" to rect(900f, 154f, 840f, 150f),
                ),
            ),
            "a2" to LiveAreaStyleSpec(
                gateRect = rect(620f, 395f, 280f, 158f),
                frameRects = mapOf(
                    "frame1" to rect(900f, 404f, 260f, 400f),
                    "frame2" to rect(320f, 404f, 260f, 400f),
                    "frame3" to rect(640f, 204f, 320f, 200f),
                ),
            ),
            "a3" to LiveAreaStyleSpec(
                gateRect = rect(620f, 395f, 280f, 158f),
                frameRects = mapOf(
                    "frame1" to rect(900f, 414f, 260f, 200f),
                    "frame2" to rect(320f, 414f, 260f, 200f),
                    "frame3" to rect(900f, 214f, 260f, 210f),
                    "frame4" to rect(640f, 214f, 320f, 210f),
                    "frame5" to rect(320f, 214f, 260f, 210f),
                ),
            ),
            "a4" to LiveAreaStyleSpec(
                gateRect = rect(620f, 395f, 280f, 158f),
                frameRects = mapOf(
                    "frame1" to rect(900f, 414f, 260f, 200f),
                    "frame2" to rect(320f, 414f, 260f, 200f),
                    "frame3" to rect(900f, 214f, 840f, 70f),
                    "frame4" to rect(900f, 144f, 840f, 70f),
                    "frame5" to rect(900f, 74f, 840f, 70f),
                ),
            ),
            "a5" to LiveAreaStyleSpec(
                gateRect = rect(380f, 395f, 280f, 158f),
                frameRects = mapOf(
                    "frame1" to rect(900f, 412f, 480f, 68f),
                    "frame2" to rect(900f, 344f, 480f, 68f),
                    "frame3" to rect(900f, 276f, 480f, 68f),
                    "frame4" to rect(900f, 208f, 480f, 68f),
                    "frame5" to rect(900f, 140f, 480f, 68f),
                    "frame6" to rect(900f, 72f, 480f, 68f),
                    "frame7" to rect(420f, 214f, 360f, 210f),
                ),
            ),
            "browser" to LiveAreaStyleSpec(
                gateRect = rect(381f, 395f, 280f, 158f),
                frameRects = mapOf(
                    "frame1" to rect(890f, 405f, 472f, 50f),
                    "frame2" to rect(890f, 355f, 472f, 50f),
                    "frame3" to rect(890f, 305f, 472f, 50f),
                    "frame4" to rect(890f, 255f, 472f, 50f),
                    "frame5" to rect(890f, 205f, 472f, 50f),
                    "frame6" to rect(890f, 155f, 472f, 50f),
                    "frame7" to rect(890f, 105f, 472f, 50f),
                    "frame8" to rect(890f, 55f, 472f, 50f),
                    "frame9" to rect(381f, 223f, 282f, 108f),
                    "frame10" to rect(381f, 115f, 282f, 108f),
                ),
            ),
            "content_manager" to LiveAreaStyleSpec(
                gateRect = rect(620f, 361f, 280f, 158f),
                frameRects = emptyMap(),
            ),
            "psmobile" to LiveAreaStyleSpec(
                gateRect = rect(380f, 345f, 280f, 158f),
                frameRects = mapOf(
                    "frame1" to rect(866f, 414f, 446f, 154f),
                    "frame2" to rect(866f, 249.5f, 446f, 109f),
                    "frame3" to rect(866f, 119f, 196f, 58f),
                    "frame4" to rect(866f, 34f, 772f, 30f),
                ),
            ),
        )

        fun resolve(style: String): LiveAreaStyleSpec =
            specs[style.lowercase(Locale.ROOT)] ?: specs.getValue("a3")
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
        // Metrics matched against genuine SceShell Home frames rendered by the
        // emulated firmware on the same panel (tools/shell-research/completion-fix-home.png):
        // Rodin LATIN Regular (ltn0) at an effective 19 px em with ~0.5 px glyph
        // emboldening (IoU 0.70 vs 0.35 for the former plain 18 px), 23 px line
        // pitch, wrap width between 148 and 159 px, and the LAST line's baseline
        // anchored 92 px below the bubble centre (two-line titles grow upward).
        val paint = TextPaint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.WHITE
            textSize = HOME_LABEL_TEXT_SIZE
            typeface = homeTitleTypeface
            style = Paint.Style.FILL_AND_STROKE
            strokeWidth = HOME_LABEL_EMBOLDEN
            strokeJoin = Paint.Join.ROUND
            setShadowLayer(2.2f, 0.8f, 1.5f, 0xD8000000.toInt())
        }
        // Wrap greedily into at most two lines; an overflowing second line is
        // cut at a character boundary with trailing spaces removed before the
        // ellipsis, as the original shell shows "Speed™ Most…".
        val width = HOME_LABEL_MAX_WIDTH
        val wrapped = StaticLayout.Builder.obtain(title, 0, title.length, paint, width.toInt())
            .setAlignment(Layout.Alignment.ALIGN_CENTER)
            .setIncludePad(false)
            .build()
        val lines = ArrayList<String>(2)
        for (line in 0 until minOf(2, wrapped.lineCount)) {
            val end = if (line == 1) title.length else wrapped.getLineEnd(line)
            lines += title.substring(wrapped.getLineStart(line), end).trim()
        }
        if (lines.size == 2 && (wrapped.lineCount > 2 || paint.measureText(lines[1]) > width)) {
            var cut = lines[1]
            while (cut.isNotEmpty() && paint.measureText(cut.trimEnd() + ELLIPSIS) > width) cut = cut.dropLast(1)
            lines[1] = cut.trimEnd() + ELLIPSIS
        } else if (lines.size == 1 && paint.measureText(lines[0]) > width) {
            var cut = lines[0]
            while (cut.isNotEmpty() && paint.measureText(cut.trimEnd() + ELLIPSIS) > width) cut = cut.dropLast(1)
            lines[0] = cut.trimEnd() + ELLIPSIS
        }
        val bitmap = Bitmap.createBitmap(
            HOME_LABEL_BITMAP_WIDTH, HOME_LABEL_HEIGHT, Bitmap.Config.ARGB_8888,
        )
        val canvas = Canvas(bitmap)
        lines.forEachIndexed { index, text ->
            val baseline = HOME_LABEL_LAST_BASELINE - (lines.size - 1 - index) * HOME_LABEL_LINE_PITCH
            canvas.drawText(text, (HOME_LABEL_BITMAP_WIDTH - paint.measureText(text)) * .5f, baseline, paint)
        }
        return bitmap
    }

    private val homeTitleTypeface: Typeface by lazy {
        Typeface.createFromFile(File(resources, "fonts/ltn0.otf"))
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
        val instant = Instant.ofEpochSecond(status.epochMinute * 60)
        val zone = ZoneId.of(status.timeZoneId)
        val locale = Locale.getDefault()
        val timePattern = if (status.use24HourClock) "HH:mm" else "h:mm"
        val timeText = DateTimeFormatter.ofPattern(timePattern, locale).withZone(zone).format(instant)
        val amPmText = if (status.use24HourClock) "" else {
            DateTimeFormatter.ofPattern("a", locale).withZone(zone).format(instant)
        }
        val combinedKey = "$timeText|$amPmText"
        if (combinedKey == previousClock) return
        val paint = clockPaint
        val amPmPaint = clockAmPmPaint
        val timeWidth = paint.measureText(timeText)
        val gap = if (amPmText.isNotEmpty()) 4f else 0f
        val amPmWidth = if (amPmText.isNotEmpty()) amPmPaint.measureText(amPmText) else 0f
        val width = ceil(timeWidth + gap + amPmWidth).toInt() + 6
        val bitmap = Bitmap.createBitmap(width, 32, Bitmap.Config.ARGB_8888)
        try {
            val baseline = (32f - paint.ascent() - paint.descent()) * .5f
            val canvas = Canvas(bitmap)
            canvas.drawText(timeText, 3f, baseline, paint)
            if (amPmText.isNotEmpty()) {
                canvas.drawText(amPmText, 3f + timeWidth + gap, baseline, amPmPaint)
            }
            send(CLOCK_IMAGE, bitmap)
            previousClock = combinedKey
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
        const val HOME_FOCUS_RING_IMAGE = 0xcf115394.toInt()
        const val HOME_PAGE_DOT_ACTIVE = 0x21ad6d05
        const val HOME_PAGE_DOT_INACTIVE = 0x266627f6
        const val LOCK_EMPTY_BG_IMAGE = 0xca707736.toInt()
        const val STATUS_BAR_BG = 0xbef34d7f.toInt()
        const val STATUS_HOME_ICON = 0x56aebd5f
        const val STATUS_NOTIF_ORB_IDLE = 0x6821404a
        const val STATUS_NOTIF_ORB_GLOW = 0x94de0b89.toInt()
        const val STATUS_NOTIF_ORB_ACTIVE = 0xe062c41f.toInt()
        const val STATUS_APP_CHIP_MASK = 0xfbc7297e.toInt()
        const val TOPMENU_ARROW_LEFT = 0x8ca9d181.toInt()
        const val TOPMENU_ARROW_RIGHT = 0x168e9e6b
        const val EDIT_NEW_PAGE_ICON = 0x23ab06c5
        const val EDIT_WALLPAPER_ICON = 0x02024c2c
        const val EDIT_DOTS_BADGE = 0xfede49bf.toInt()
        const val EDIT_CALLOUT_BG = 0xd29cb815.toInt()
        const val EDIT_BACK_ICON = 0x2cac72e8
        const val FOLDER_PANEL_BG = 0xd8cdfae1.toInt()
        const val FOLDER_ICON = 0xe35fe13a.toInt()
        const val FOLDER_TITLE_PILL = 0xf1455f7e.toInt()
        const val LIVEAREA_GATE_GLOW_IMAGE = 0xd5dd35e7.toInt()
        const val LIVEAREA_START_CONTROL_IMAGE = 0x1e6a33de
        const val HOME_CHIP_IMAGE_BASE = 40000
        const val FOLDER_LABEL_IMAGE = 45000
        const val EDIT_CREATE_FOLDER_LABEL = 45001
        const val EDIT_UNGROUP_LABEL = 45002
        const val EDIT_DONE_LABEL = 45003
        const val NOTIFICATION_TEXT_IMAGE = 45010
        private const val LIVEAREA_IMAGE_BASE = 3000
        private const val LIVEAREA_IMAGE_SLOTS_PER_GAME = 64
        private const val HOME_CATALOG_MAX_GAMES = 512
        private const val MAX_LIVEAREA_TEMPLATE_BYTES = 1024L * 1024L
        private val DOCTYPE_PATTERN = Regex("<!\\s*DOCTYPE\\b", RegexOption.IGNORE_CASE)
        private const val HOME_LABEL_MAX_WIDTH = 152f
        private const val HOME_LABEL_BITMAP_WIDTH = 168
        private const val HOME_LABEL_HEIGHT = 60
        // The native renderer places the label bitmap top 40 px below the
        // bubble centre; the last baseline lands 92 px below it.
        private const val HOME_LABEL_LAST_BASELINE = 52f
        private const val HOME_LABEL_TEXT_SIZE = 19f
        private const val HOME_LABEL_EMBOLDEN = 1.0f
        private const val HOME_LABEL_LINE_PITCH = 23f
        private const val ELLIPSIS = "…"
    }
}
