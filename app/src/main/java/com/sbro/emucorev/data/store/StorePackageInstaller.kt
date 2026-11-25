package com.sbro.emucorev.data.store

import android.content.Context
import android.graphics.BitmapFactory
import android.graphics.Color
import android.graphics.Typeface
import android.media.MediaMetadataRetriever
import com.sbro.emucorev.data.theme.ThemePackageRepository
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream
import java.util.zip.ZipInputStream

class StorePackageException(message: String) : IllegalArgumentException(message)

@Serializable
internal data class WallpaperManifest(
    val formatVersion: Int = 0,
    val id: String = "",
    val version: Int = 1,
    val type: String = "image",
    /** 0..1; the Home background dim preference is this * 100, capped at 85. */
    val dim: Float = 0.35f,
    val focus: String = "center"
)

@Serializable
internal data class SoundsManifest(
    val formatVersion: Int = 0,
    val id: String = "",
    val version: Int = 1,
    /** key (click, menu_open, menu_close, menu_select, music, achievement) -> archive file name. */
    val files: Map<String, String> = emptyMap()
)

@Serializable
internal data class FontManifest(
    val formatVersion: Int = 0,
    val id: String = "",
    /** Optional in packages; the installer stores the catalog item's version here. */
    val version: Int = 1,
    /** Display name; becomes the custom font name shown in Settings. */
    val family: String = "",
    /** font.ttf or font.otf. */
    val file: String = "",
    /** OFL-1.1 or Apache-2.0. */
    val license: String = "",
    val source: String = "",
    /** latin, cyrillic, greek, ... */
    val scripts: List<String> = emptyList(),
    /** 0.6..1.4; validated and kept in the installed manifest. */
    val sizeScale: Float = 1f
)

@Serializable
internal data class ControlsManifest(
    val formatVersion: Int = 0,
    val id: String = "",
    val version: Int = 1,
    val skin: ControlsSkinSpec? = null,
    val layout: ControlsLayoutSpec? = null
)

@Serializable
internal data class ControlsSkinSpec(
    /** control id (or "<id>_pressed") -> archive image file name. */
    val images: Map<String, String> = emptyMap(),
    /** "#RRGGBB" or "#AARRGGBB". */
    val tint: String? = null,
    /** 0..1. */
    val opacity: Float = 1f,
    /** grow, shrink, spring or glow. */
    val pressEffect: String? = null
)

@Serializable
internal data class ControlsLayoutSpec(
    /** Standard overlay: control id -> {x, y, scale, widthScale, opacity, visible, surfaceOnly, secondaryActionId}. */
    val overlay: JsonObject? = null,
    /** A CustomTouchControlLibrary document. */
    val custom: JsonObject? = null
)

/**
 * Validates and installs downloaded Store packages. Every archive is fully read and checked in memory
 * (allow-listed flat entry names, per-file and total caps, JSON manifest required) before anything is
 * written, and files land via a staging directory so a failed install never leaves half a package.
 */
class StorePackageInstaller private constructor(context: Context) {
    private val appContext = context.applicationContext
    private val installs = StoreInstalls.getInstance(appContext)

    /**
     * Installs [file] (a verified download of [item]) and deletes it afterwards. Reinstalling the same or a
     * newer version replaces the old files; an older version is refused.
     */
    suspend fun install(item: StoreItem, file: File): Result<InstalledStoreItem> = withContext(Dispatchers.IO) {
        try {
            Result.success(installBlocking(item, file))
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (error: Exception) {
            Result.failure(error)
        } finally {
            file.delete()
        }
    }

    @Synchronized
    private fun installBlocking(item: StoreItem, file: File): InstalledStoreItem {
        if (!StoreInstalls.ID_PATTERN.matches(item.id)) throw StorePackageException("Invalid Store item id")
        if (!file.isFile || file.length() == 0L || file.length() > StoreRepository.maxPackageBytes(item.kind)) {
            throw StorePackageException("Package file is missing or too large")
        }
        when (item.kind) {
            StoreKind.THEME -> installTheme(item, file)
            StoreKind.WALLPAPER -> installWallpaper(item, file)
            StoreKind.SOUNDS -> installSounds(item, file)
            StoreKind.CONTROLS -> installControls(item, file)
            StoreKind.FONT -> installFont(item, file)
        }
        installs.refresh()
        return installs.find(item.id) ?: throw StorePackageException("Installed package is not readable")
    }

    // region Theme

    private fun installTheme(item: StoreItem, file: File) {
        val repository = ThemePackageRepository.getInstance(appContext)
        val installed = kotlinx.coroutines.runBlocking { repository.install(file.readBytes()) }
            .getOrElse { throw StorePackageException(it.message ?: "Invalid theme package") }
        if (installed.id != item.id) {
            repository.reload()
            kotlinx.coroutines.runBlocking { repository.remove(installed.id) }
            throw StorePackageException("Theme id does not match the Store item")
        }
    }

    // endregion

    // region Wallpaper

    private fun installWallpaper(item: StoreItem, file: File) {
        val entries = readArchive(file, maxEntries = 2, maxTotal = 5L * 1024 * 1024) { name ->
            when (name) {
                StoreInstalls.WALLPAPER_JSON -> 16L * 1024
                in WALLPAPER_IMAGES -> 4L * 1024 * 1024
                else -> null
            }
        }
        val manifest = decode<WallpaperManifest>(entries[StoreInstalls.WALLPAPER_JSON], "wallpaper.json")
        requireHeader(item, manifest.formatVersion, manifest.id, manifest.version)
        if (manifest.type != "image") throw StorePackageException("Unsupported wallpaper type")
        if (manifest.dim !in 0f..1f) throw StorePackageException("Invalid wallpaper dim")
        val imageName = WALLPAPER_IMAGES.filter(entries::containsKey).singleOrNull()
            ?: throw StorePackageException("Wallpaper image is missing")
        checkBounds(entries.getValue(imageName), minSide = 1, maxSide = MAX_WALLPAPER_SIDE, maxAspect = Float.MAX_VALUE, label = "wallpaper")
        commit(StoreKind.WALLPAPER, item.id) { staging ->
            entries.forEach { (name, bytes) -> File(staging, name).writeBytes(bytes) }
        }
    }

    // endregion

    // region Sounds

    /** Reads and validates a sound package archive in memory; shared by install and Store previews. */
    private fun readSoundPack(item: StoreItem, file: File): Pair<SoundsManifest, Map<String, ByteArray>> {
        val allowedNames = StoreSoundPacks.KEYS.flatMap { key -> listOf("$key.ogg", "$key.wav") }.toSet()
        val entries = readArchive(file, maxEntries = StoreSoundPacks.KEYS.size + 1, maxTotal = 7L * 1024 * 1024) { name ->
            when {
                name == StoreInstalls.SOUNDS_JSON -> 16L * 1024
                name in allowedNames -> when {
                    name.startsWith("music.") -> MAX_MUSIC_BYTES
                    name.startsWith("achievement.") -> MAX_ACHIEVEMENT_BYTES
                    else -> MAX_SHORT_BYTES
                }
                else -> null
            }
        }
        val manifest = decode<SoundsManifest>(entries[StoreInstalls.SOUNDS_JSON], "sounds.json")
        requireHeader(item, manifest.formatVersion, manifest.id, manifest.version)
        if (manifest.files.isEmpty()) throw StorePackageException("Sound pack has no sounds")
        manifest.files.forEach { (key, name) ->
            if (key !in StoreSoundPacks.KEYS) throw StorePackageException("Unknown sound: ${key.take(32)}")
            if (name != "$key.ogg" && name != "$key.wav") throw StorePackageException("Invalid file for sound $key")
            if (!entries.containsKey(name)) throw StorePackageException("Missing file for sound $key")
        }
        if (entries.keys.any { it != StoreInstalls.SOUNDS_JSON && it !in manifest.files.values }) {
            throw StorePackageException("Unreferenced file in sound pack")
        }
        return manifest to entries
    }

    /**
     * Unpacks only the validated sound files of a verified [file] into [directory] for a Store preview.
     * The package is kept; nothing is installed.
     */
    suspend fun extractSoundPreview(item: StoreItem, file: File, directory: File): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            if (item.kind != StoreKind.SOUNDS || !StoreInstalls.ID_PATTERN.matches(item.id)) {
                throw StorePackageException("Not a sound pack")
            }
            if (!file.isFile || file.length() == 0L || file.length() > StoreRepository.maxPackageBytes(item.kind)) {
                throw StorePackageException("Package file is missing or too large")
            }
            val (manifest, entries) = readSoundPack(item, file)
            directory.deleteRecursively()
            check(directory.mkdirs()) { "Unable to prepare the preview directory" }
            try {
                manifest.files.values.forEach { name -> File(directory, name).writeBytes(entries.getValue(name)) }
            } catch (error: Exception) {
                directory.deleteRecursively()
                throw error
            }
            Result.success(Unit)
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (error: Exception) {
            Result.failure(error)
        }
    }

    private fun installSounds(item: StoreItem, file: File) {
        val (manifest, entries) = readSoundPack(item, file)
        commit(StoreKind.SOUNDS, item.id) { staging ->
            entries.forEach { (name, bytes) -> File(staging, name).writeBytes(bytes) }
            manifest.files.forEach { (key, name) ->
                val maxDurationMs = when (key) {
                    "music" -> null
                    "achievement" -> MAX_ACHIEVEMENT_DURATION_MS
                    else -> MAX_SHORT_DURATION_MS
                }
                validateAudio(File(staging, name), maxDurationMs)
            }
        }
    }

    private fun validateAudio(file: File, maxDurationMs: Long?) {
        val retriever = MediaMetadataRetriever()
        try {
            retriever.setDataSource(file.absolutePath)
            if (retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_HAS_AUDIO) != "yes") {
                throw StorePackageException("Sound file has no audio")
            }
            val durationMs = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull()
            if (maxDurationMs != null && durationMs != null && durationMs > maxDurationMs) {
                throw StorePackageException("Sound effect is too long")
            }
        } catch (error: StorePackageException) {
            throw error
        } catch (error: Exception) {
            throw StorePackageException("Sound file cannot be decoded")
        } finally {
            runCatching { retriever.release() }
        }
    }

    // endregion

    // region Font

    private fun installFont(item: StoreItem, file: File) {
        val entries = readArchive(file, maxEntries = 3, maxTotal = MAX_FONT_PACKAGE_BYTES) { name ->
            when (name) {
                StoreInstalls.FONT_JSON -> 16L * 1024
                in FONT_FILES -> MAX_FONT_PACKAGE_BYTES
                FONT_LICENSE -> 256L * 1024
                else -> null
            }
        }
        val manifest = decode<FontManifest>(entries[StoreInstalls.FONT_JSON], "font.json")
        requireHeader(item, manifest.formatVersion, manifest.id, item.version)
        val family = manifest.family.trim()
        if (family.isEmpty() || family.length > 64 || family.any { it.isISOControl() }) {
            throw StorePackageException("Invalid font family")
        }
        if (manifest.file !in FONT_FILES) throw StorePackageException("Invalid font file name")
        val fontBytes = entries[manifest.file] ?: throw StorePackageException("Font file is missing")
        if (entries.keys.count { it in FONT_FILES } != 1) throw StorePackageException("Package must contain exactly one font")
        if (!entries.containsKey(FONT_LICENSE)) throw StorePackageException("Font license is missing")
        if (manifest.license !in FONT_LICENSES) throw StorePackageException("Unsupported font license")
        if (manifest.source.isNotEmpty() && (!manifest.source.startsWith("https://") || manifest.source.length > 300)) {
            throw StorePackageException("Invalid font source")
        }
        if (manifest.scripts.size > 12 || manifest.scripts.any { !FONT_SCRIPT.matches(it) }) {
            throw StorePackageException("Invalid font scripts")
        }
        if (manifest.sizeScale !in 0.6f..1.4f) throw StorePackageException("Invalid font size scale")
        if (!hasFontSignature(fontBytes)) throw StorePackageException("Not a TrueType or OpenType font")
        commit(StoreKind.FONT, item.id) { staging ->
            entries.forEach { (name, bytes) -> if (name != StoreInstalls.FONT_JSON) File(staging, name).writeBytes(bytes) }
            // Keep the catalog version so updates are detected; defaults must be written for it to survive.
            File(staging, StoreInstalls.FONT_JSON).writeText(
                FONT_JSON_OUT.encodeToString(FontManifest.serializer(), manifest.copy(version = item.version))
            )
            // Same check the manual custom-font import uses.
            val loads = runCatching { Typeface.Builder(File(staging, manifest.file)).build() }.getOrNull() != null
            if (!loads) throw StorePackageException("Font cannot be decoded")
        }
    }

    /** TrueType (0x00010000, "true"), OpenType/CFF ("OTTO") or a collection ("ttcf") header. */
    private fun hasFontSignature(bytes: ByteArray): Boolean {
        if (bytes.size < 12) return false
        val tag = String(bytes, 0, 4, Charsets.ISO_8859_1)
        return tag == "OTTO" || tag == "true" || tag == "ttcf" ||
            (bytes[0] == 0.toByte() && bytes[1] == 1.toByte() && bytes[2] == 0.toByte() && bytes[3] == 0.toByte())
    }

    // endregion

    // region Controls

    private fun installControls(item: StoreItem, file: File) {
        val entries = readArchive(file, maxEntries = MAX_CONTROL_IMAGES + 1, maxTotal = 6L * 1024 * 1024) { name ->
            when {
                name == StoreInstalls.CONTROLS_JSON -> 256L * 1024
                CONTROL_IMAGE_NAME.matches(name) -> 768L * 1024
                else -> null
            }
        }
        val manifest = decode<ControlsManifest>(entries[StoreInstalls.CONTROLS_JSON], "controls.json")
        requireHeader(item, manifest.formatVersion, manifest.id, manifest.version)
        val skin = manifest.skin
        val layout = manifest.layout
        if ((skin == null || skin.images.isEmpty()) && layout?.overlay == null && layout?.custom == null) {
            throw StorePackageException("Controls package has no skin or layout")
        }
        if (skin != null) {
            if (skin.images.size > MAX_CONTROL_IMAGES) throw StorePackageException("Too many skin images")
            skin.images.forEach { (controlId, name) ->
                if (!CONTROL_ID.matches(controlId)) throw StorePackageException("Invalid control id")
                val bytes = entries[name] ?: throw StorePackageException("Missing skin image for $controlId")
                checkBounds(bytes, minSide = 64, maxSide = 1024, maxAspect = 1.5f, label = "skin image")
            }
            if (entries.keys.any { it != StoreInstalls.CONTROLS_JSON && it !in skin.images.values }) {
                throw StorePackageException("Unreferenced file in controls package")
            }
            skin.tint?.let { tint ->
                runCatching { Color.parseColor(tint) }.getOrNull() ?: throw StorePackageException("Invalid skin tint")
            }
            if (skin.opacity !in 0f..1f) throw StorePackageException("Invalid skin opacity")
            if (skin.pressEffect != null && skin.pressEffect.lowercase() !in PRESS_EFFECTS) {
                throw StorePackageException("Invalid press effect")
            }
        } else if (entries.keys.any { it != StoreInstalls.CONTROLS_JSON }) {
            throw StorePackageException("Unexpected files in controls package")
        }
        layout?.overlay?.let { overlay ->
            if (overlay.size > 64 || overlay.keys.any { !CONTROL_ID.matches(it) }) {
                throw StorePackageException("Invalid overlay layout")
            }
        }
        // Custom button libraries are not supported in this app; they are validated by the apps that apply them.
        if (skin == null) throw StorePackageException("This app only uses controls skins")
        commit(StoreKind.CONTROLS, item.id) { staging ->
            entries.forEach { (name, bytes) -> File(staging, name).writeBytes(bytes) }
        }
    }

    // endregion

    // region Shared

    private fun requireHeader(item: StoreItem, formatVersion: Int, id: String, version: Int) {
        if (formatVersion != 1) throw StorePackageException("Unsupported package format")
        if (id != item.id) throw StorePackageException("Package id does not match the Store item")
        if (version < 1) throw StorePackageException("Invalid package version")
        val existing = installs.find(item.id)
        if (existing != null && existing.version > version) {
            throw StorePackageException("A newer version is already installed")
        }
    }

    private inline fun <reified T> decode(bytes: ByteArray?, label: String): T {
        if (bytes == null) throw StorePackageException("$label is missing")
        return try {
            StoreInstalls.JSON.decodeFromString<T>(bytes.decodeToString())
        } catch (error: Exception) {
            throw StorePackageException("Invalid $label")
        }
    }

    private fun checkBounds(bytes: ByteArray, minSide: Int, maxSide: Int, maxAspect: Float, label: String) {
        val options = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(bytes, 0, bytes.size, options)
        val w = options.outWidth
        val h = options.outHeight
        if (w < minSide || h < minSide || w > maxSide || h > maxSide) throw StorePackageException("Invalid $label size")
        if (maxOf(w, h).toFloat() / minOf(w, h) > maxAspect) throw StorePackageException("Invalid $label proportions")
    }

    /** Writes into `.staging-<id>` via [writer], then swaps it in for `<kind>/<id>`. */
    private fun commit(kind: StoreKind, id: String, writer: (File) -> Unit) {
        val root = StoreInstalls.kindDirectory(appContext, kind)
        root.mkdirs()
        val staging = File(root, ".staging-$id")
        staging.deleteRecursively()
        check(staging.mkdirs()) { "Unable to prepare the install directory" }
        try {
            writer(staging)
            val target = File(root, id)
            target.deleteRecursively()
            check(staging.renameTo(target)) { "Unable to save the package" }
        } finally {
            staging.deleteRecursively()
        }
    }

    /** Flat, allow-listed entries only; [limitFor] returns the per-file byte cap or null to reject the name. */
    private fun readArchive(
        file: File,
        maxEntries: Int,
        maxTotal: Long,
        limitFor: (String) -> Long?
    ): Map<String, ByteArray> {
        val entries = LinkedHashMap<String, ByteArray>()
        var total = 0L
        try {
            ZipInputStream(file.inputStream().buffered()).use { zip ->
                while (true) {
                    val entry = zip.nextEntry ?: break
                    val name = entry.name
                    val limit = if (entry.isDirectory) null else limitFor(name)
                        ?: throw StorePackageException("Unexpected file in package: ${name.take(64)}")
                    if (limit == null) throw StorePackageException("Unexpected directory in package")
                    if (entries.containsKey(name)) throw StorePackageException("Duplicate file in package")
                    if (entries.size >= maxEntries) throw StorePackageException("Too many files in package")
                    val bytes = readLimited(zip, limit)
                    total += bytes.size
                    if (total > maxTotal) throw StorePackageException("Package is too large")
                    entries[name] = bytes
                }
            }
        } catch (error: StorePackageException) {
            throw error
        } catch (error: java.io.IOException) {
            throw StorePackageException("Package is not a valid archive")
        }
        if (entries.isEmpty()) throw StorePackageException("Package is empty")
        return entries
    }

    private fun readLimited(input: InputStream, limit: Long): ByteArray {
        val output = ByteArrayOutputStream()
        val buffer = ByteArray(16 * 1024)
        var total = 0L
        while (true) {
            val read = input.read(buffer)
            if (read < 0) break
            total += read
            if (total > limit) throw StorePackageException("A file in the package is too large")
            output.write(buffer, 0, read)
        }
        return output.toByteArray()
    }

    // endregion

    companion object {
        internal val WALLPAPER_IMAGES = listOf("wallpaper.webp", "wallpaper.jpg", "wallpaper.png")
        internal val CONTROL_ID = Regex("[a-z0-9_]{1,40}")
        private val CONTROL_IMAGE_NAME = Regex("[a-z0-9_-]{1,48}\\.(png|webp)")
        internal val PRESS_EFFECTS = setOf("grow", "shrink", "spring", "glow")
        private const val MAX_CONTROL_IMAGES = 48
        private const val MAX_WALLPAPER_SIDE = 2560
        private const val MAX_SHORT_BYTES = 300L * 1024
        private const val MAX_MUSIC_BYTES = 6L * 1024 * 1024
        private const val MAX_ACHIEVEMENT_BYTES = 600L * 1024
        // The whole font package is capped at 4 MB (StoreRepository.maxPackageBytes); a font is most of it.
        private const val MAX_FONT_PACKAGE_BYTES = 4L * 1024 * 1024
        internal val FONT_FILES = setOf("font.ttf", "font.otf")
        internal const val FONT_LICENSE = "LICENSE.txt"
        private val FONT_LICENSES = setOf("OFL-1.1", "Apache-2.0")
        private val FONT_SCRIPT = Regex("[a-z][a-z-]{1,23}") // e.g. "latin-ext"
        private val FONT_JSON_OUT = Json {
            encodeDefaults = true
            explicitNulls = false
        }
        // README: short sounds are at most 2 s; a little slack for encoder padding.
        private const val MAX_SHORT_DURATION_MS = 2_250L
        // Achievement jingles may run a little longer than UI clicks.
        private const val MAX_ACHIEVEMENT_DURATION_MS = 6_000L

        @Volatile private var instance: StorePackageInstaller? = null

        fun getInstance(context: Context): StorePackageInstaller = instance ?: synchronized(this) {
            instance ?: StorePackageInstaller(context).also { instance = it }
        }
    }
}
