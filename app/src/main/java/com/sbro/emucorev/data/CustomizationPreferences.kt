package com.sbro.emucorev.data

import android.content.Context
import android.content.SharedPreferences
import android.graphics.Typeface
import android.graphics.BitmapFactory
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.provider.OpenableColumns
import androidx.core.content.edit
import java.io.Closeable
import java.io.File
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

enum class AppFont {
    SYSTEM,
    RUBIK,
    EXO2,
    CUSTOM
}

/** Visual treatment only; controller geometry and touch targets never depend on this value. */
enum class TouchControlVisualStyle {
    CLASSIC,
    LEGACY,
    MODERN,
    ARCADE,
    MINIMAL
}

/** Press animation only; input dispatch and touch targets never depend on this value. */
enum class TouchControlPressEffect {
    GROW,
    SHRINK,
    SPRING,
    GLOW
}

/** Changes only the presentation of the in-game menu; every action remains available. */
enum class GameMenuLayoutStyle {
    SIDEBAR,
    DASHBOARD,
    COMMAND_CENTER,
    COMPACT
}

/** Visual and density treatment for the application navigation drawer. */
enum class DrawerVisualStyle {
    CLASSIC,
    COMPACT,
    GLASS,
    CONSOLE
}

data class CustomizationSettings(
    val backgroundPath: String? = null,
    val backgroundMimeType: String? = null,
    val coverSizePercent: Int = DEFAULT_COVER_SIZE_PERCENT,
    val appFont: AppFont = AppFont.SYSTEM,
    val customFontPath: String? = null,
    val textSizePercent: Int = DEFAULT_TEXT_SIZE_PERCENT,
    val touchControlVisualStyle: TouchControlVisualStyle = TouchControlVisualStyle.CLASSIC,
    val touchControlPressEffect: TouchControlPressEffect = TouchControlPressEffect.GROW,
    val gameMenuLayoutStyle: GameMenuLayoutStyle = GameMenuLayoutStyle.SIDEBAR,
    val drawerVisualStyle: DrawerVisualStyle = DrawerVisualStyle.CLASSIC
) {
    companion object {
        const val DEFAULT_COVER_SIZE_PERCENT = 100
        const val MIN_COVER_SIZE_PERCENT = 70
        const val MAX_COVER_SIZE_PERCENT = 150
        const val DEFAULT_TEXT_SIZE_PERCENT = 100
        const val MIN_TEXT_SIZE_PERCENT = 85
        const val MAX_TEXT_SIZE_PERCENT = 130
    }
}

class CustomizationPreferences(context: Context) : Closeable {
    private val prefs = context.applicationContext.getSharedPreferences(
        PREFERENCES_NAME,
        Context.MODE_PRIVATE
    )
    private val listener = SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
        if (key in CUSTOMIZATION_KEYS) {
            _settings.value = readSettings()
        }
    }
    private val _settings = MutableStateFlow(readSettings())

    val settings: StateFlow<CustomizationSettings> = _settings.asStateFlow()
    val current: CustomizationSettings
        get() = _settings.value

    init {
        prefs.registerOnSharedPreferenceChangeListener(listener)
    }

    fun setBackground(path: String, mimeType: String) {
        prefs.edit {
            putString(KEY_BACKGROUND_PATH, path)
            putString(KEY_BACKGROUND_MIME_TYPE, mimeType)
        }
        _settings.value = readSettings()
    }

    /**
     * Forgets the current background.
     *
     * Called when a background turns out to be unrenderable, so the app can
     * recover on its own instead of failing again on the next launch.
     */
    fun clearBackground() {
        prefs.edit {
            remove(KEY_BACKGROUND_PATH)
            remove(KEY_BACKGROUND_MIME_TYPE)
        }
        _settings.value = readSettings()
    }

    fun setCoverSizePercent(value: Int) {
        prefs.edit {
            putInt(
                KEY_COVER_SIZE_PERCENT,
                value.coerceIn(
                    CustomizationSettings.MIN_COVER_SIZE_PERCENT,
                    CustomizationSettings.MAX_COVER_SIZE_PERCENT
                )
            )
        }
    }

    fun setAppFont(font: AppFont, customFontPath: String? = current.customFontPath) {
        prefs.edit {
            putString(KEY_APP_FONT, font.name)
            if (customFontPath.isNullOrBlank()) remove(KEY_CUSTOM_FONT_PATH)
            else putString(KEY_CUSTOM_FONT_PATH, customFontPath)
        }
    }

    fun setTextSizePercent(value: Int) {
        prefs.edit {
            putInt(
                KEY_TEXT_SIZE_PERCENT,
                value.coerceIn(
                    CustomizationSettings.MIN_TEXT_SIZE_PERCENT,
                    CustomizationSettings.MAX_TEXT_SIZE_PERCENT
                )
            )
        }
    }

    fun setTouchControlVisualStyle(style: TouchControlVisualStyle) {
        prefs.edit { putString(KEY_TOUCH_CONTROL_VISUAL_STYLE, style.name) }
    }

    fun setTouchControlPressEffect(effect: TouchControlPressEffect) {
        prefs.edit { putString(KEY_TOUCH_CONTROL_PRESS_EFFECT, effect.name) }
    }

    fun setGameMenuLayoutStyle(style: GameMenuLayoutStyle) {
        prefs.edit { putString(KEY_GAME_MENU_LAYOUT_STYLE, style.name) }
    }

    fun setDrawerVisualStyle(style: DrawerVisualStyle) {
        prefs.edit { putString(KEY_DRAWER_VISUAL_STYLE, style.name) }
    }

    fun reset() {
        prefs.edit {
            CUSTOMIZATION_KEYS.forEach(::remove)
        }
    }

    override fun close() {
        prefs.unregisterOnSharedPreferenceChangeListener(listener)
    }

    private fun readSettings(): CustomizationSettings {
        val customFontPath = prefs.getString(KEY_CUSTOM_FONT_PATH, null)
            ?.takeIf { File(it).isFile }
        val requestedFont = runCatching {
            AppFont.valueOf(prefs.getString(KEY_APP_FONT, AppFont.SYSTEM.name).orEmpty())
        }.getOrDefault(AppFont.SYSTEM)
        return CustomizationSettings(
            backgroundPath = prefs.getString(KEY_BACKGROUND_PATH, null)
                ?.takeIf { File(it).isFile },
            backgroundMimeType = prefs.getString(KEY_BACKGROUND_MIME_TYPE, null),
            coverSizePercent = prefs.getInt(
                KEY_COVER_SIZE_PERCENT,
                CustomizationSettings.DEFAULT_COVER_SIZE_PERCENT
            ).coerceIn(
                CustomizationSettings.MIN_COVER_SIZE_PERCENT,
                CustomizationSettings.MAX_COVER_SIZE_PERCENT
            ),
            appFont = if (requestedFont == AppFont.CUSTOM && customFontPath == null) {
                AppFont.SYSTEM
            } else {
                requestedFont
            },
            customFontPath = customFontPath,
            textSizePercent = prefs.getInt(
                KEY_TEXT_SIZE_PERCENT,
                CustomizationSettings.DEFAULT_TEXT_SIZE_PERCENT
            ).coerceIn(
                CustomizationSettings.MIN_TEXT_SIZE_PERCENT,
                CustomizationSettings.MAX_TEXT_SIZE_PERCENT
            ),
            touchControlVisualStyle = prefs.enumValue(
                KEY_TOUCH_CONTROL_VISUAL_STYLE,
                TouchControlVisualStyle.CLASSIC
            ),
            touchControlPressEffect = prefs.enumValue(
                KEY_TOUCH_CONTROL_PRESS_EFFECT,
                TouchControlPressEffect.GROW
            ),
            gameMenuLayoutStyle = prefs.enumValue(
                KEY_GAME_MENU_LAYOUT_STYLE,
                GameMenuLayoutStyle.SIDEBAR
            ),
            drawerVisualStyle = prefs.enumValue(
                KEY_DRAWER_VISUAL_STYLE,
                DrawerVisualStyle.CLASSIC
            )
        )
    }

    private inline fun <reified T : Enum<T>> SharedPreferences.enumValue(
        key: String,
        fallback: T
    ): T = runCatching {
        enumValueOf<T>(getString(key, fallback.name).orEmpty())
    }.getOrDefault(fallback)

    private companion object {
        const val PREFERENCES_NAME = "customization_preferences"
        const val KEY_BACKGROUND_PATH = "background_path"
        const val KEY_BACKGROUND_MIME_TYPE = "background_mime_type"
        const val KEY_COVER_SIZE_PERCENT = "cover_size_percent"
        const val KEY_APP_FONT = "app_font"
        const val KEY_CUSTOM_FONT_PATH = "custom_font_path"
        const val KEY_TEXT_SIZE_PERCENT = "text_size_percent"
        const val KEY_TOUCH_CONTROL_VISUAL_STYLE = "touch_control_visual_style"
        const val KEY_TOUCH_CONTROL_PRESS_EFFECT = "touch_control_press_effect"
        const val KEY_GAME_MENU_LAYOUT_STYLE = "game_menu_layout_style"
        const val KEY_DRAWER_VISUAL_STYLE = "drawer_visual_style"
        val CUSTOMIZATION_KEYS = setOf(
            KEY_BACKGROUND_PATH,
            KEY_BACKGROUND_MIME_TYPE,
            KEY_COVER_SIZE_PERCENT,
            KEY_APP_FONT,
            KEY_CUSTOM_FONT_PATH,
            KEY_TEXT_SIZE_PERCENT,
            KEY_TOUCH_CONTROL_VISUAL_STYLE,
            KEY_TOUCH_CONTROL_PRESS_EFFECT,
            KEY_GAME_MENU_LAYOUT_STYLE,
            KEY_DRAWER_VISUAL_STYLE
        )
    }
}

data class ImportedCustomizationFile(
    val path: String,
    val mimeType: String
)

class CustomizationFileStore(private val context: Context) {
    private val directory = File(context.filesDir, "customization")

    fun importBackground(uri: Uri): ImportedCustomizationFile {
        val displayName = displayName(uri)
        val extension = displayName.substringAfterLast('.', "").lowercase()
        val mimeType = context.contentResolver.getType(uri)
            ?: backgroundMimeType(extension)
            ?: error("Unsupported background format")
        require(isSupportedBackground(mimeType, extension)) { "Unsupported background format" }
        val normalizedExtension = extension.takeIf {
            it in SUPPORTED_BACKGROUND_EXTENSIONS
        }
            ?: extensionForMimeType(mimeType)
        val temporaryPath = copyValidated(
            uri,
            "background",
            normalizedExtension,
            MAX_BACKGROUND_BYTES,
            finalize = false
        )
        val temporary = File(temporaryPath)
        val valid = if (mimeType.startsWith("video/")) {
            isValidVideo(temporary)
        } else {
            isValidImage(temporary)
        }
        if (!valid) {
            temporary.delete()
            error("Invalid background file")
        }
        return ImportedCustomizationFile(
            path = finalizeImport(temporary, "background", normalizedExtension),
            mimeType = mimeType
        )
    }

    fun importFont(uri: Uri): String {
        val extension = displayName(uri).substringAfterLast('.', "").lowercase()
        require(extension == "ttf" || extension == "otf") { "Unsupported font format" }
        val temporaryPath = copyValidated(uri, "font", extension, MAX_FONT_BYTES, finalize = false)
        val temporary = File(temporaryPath)
        runCatching { Typeface.createFromFile(temporary) }
            .getOrElse {
                temporary.delete()
                error("Invalid font file")
            }
        return finalizeImport(temporary, "font", extension)
    }

    fun clear() {
        directory.listFiles()
            ?.filter { it.name.startsWith("background.") || it.name.startsWith("font.") }
            ?.forEach(File::delete)
    }

    private fun copyValidated(
        uri: Uri,
        baseName: String,
        extension: String,
        maxBytes: Long,
        finalize: Boolean = true
    ): String {
        directory.mkdirs()
        val temporary = File(directory, "$baseName.importing")
        temporary.delete()
        var copied = 0L
        context.contentResolver.openInputStream(uri)?.use { input ->
            temporary.outputStream().buffered().use { output ->
                val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                while (true) {
                    val read = input.read(buffer)
                    if (read < 0) break
                    copied += read
                    require(copied <= maxBytes) { "Selected file is too large" }
                    output.write(buffer, 0, read)
                }
            }
        } ?: error("Could not open selected file")
        require(copied > 0L) { "Selected file is empty" }
        return if (finalize) finalizeImport(temporary, baseName, extension) else temporary.absolutePath
    }

    private fun finalizeImport(temporary: File, baseName: String, extension: String): String {
        directory.listFiles()
            ?.filter { it.name.startsWith("$baseName.") && it != temporary }
            ?.forEach(File::delete)
        val target = File(directory, "$baseName.$extension")
        Files.move(
            temporary.toPath(),
            target.toPath(),
            StandardCopyOption.REPLACE_EXISTING
        )
        return target.absolutePath
    }

    private fun displayName(uri: Uri): String {
        return context.contentResolver.query(
            uri,
            arrayOf(OpenableColumns.DISPLAY_NAME),
            null,
            null,
            null
        )?.use { cursor ->
            if (cursor.moveToFirst()) cursor.getString(0) else null
        } ?: uri.lastPathSegment.orEmpty()
    }

    companion object {
        private const val MAX_BACKGROUND_BYTES = 256L * 1024L * 1024L
        private const val MAX_FONT_BYTES = 32L * 1024L * 1024L

        /**
         * Upper bound on decoded image size (~64 MP).
         *
         * Anything larger is refused at import time rather than crashing with
         * [OutOfMemoryError] while the home screen composes.
         */
        private const val MAX_BACKGROUND_PIXELS = 64_000_000L

        fun isSupportedBackground(mimeType: String?, extension: String): Boolean {
            return mimeType?.startsWith("image/") == true ||
                mimeType?.startsWith("video/") == true ||
                extension.lowercase() in SUPPORTED_BACKGROUND_EXTENSIONS
        }

        private val SUPPORTED_BACKGROUND_EXTENSIONS = setOf(
            "jpg", "jpeg", "png", "webp", "gif", "mp4", "webm", "mkv"
        )

        private fun isValidImage(file: File): Boolean {
            val options = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(file.absolutePath, options)
            if (options.outWidth <= 0 || options.outHeight <= 0) return false

            // Reject images that cannot be decoded within a normal app heap.
            // The bounds pass already gives us the dimensions, so refusing here
            // avoids importing a file that would crash at render time.
            val pixels = options.outWidth.toLong() * options.outHeight.toLong()
            return pixels <= MAX_BACKGROUND_PIXELS
        }

        private fun isValidVideo(file: File): Boolean {
            return runCatching {
                val retriever = MediaMetadataRetriever()
                try {
                    retriever.setDataSource(file.absolutePath)
                    val width = retriever.extractMetadata(
                        MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH
                    )?.toIntOrNull() ?: 0
                    val height = retriever.extractMetadata(
                        MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT
                    )?.toIntOrNull() ?: 0
                    width > 0 && height > 0
                } finally {
                    retriever.release()
                }
            }.getOrDefault(false)
        }

        private fun backgroundMimeType(extension: String): String? = when (extension) {
            "jpg", "jpeg" -> "image/jpeg"
            "png" -> "image/png"
            "webp" -> "image/webp"
            "gif" -> "image/gif"
            "mp4" -> "video/mp4"
            "webm" -> "video/webm"
            "mkv" -> "video/x-matroska"
            else -> null
        }

        private fun extensionForMimeType(mimeType: String): String = when (mimeType) {
            "image/jpeg" -> "jpg"
            "image/png" -> "png"
            "image/webp" -> "webp"
            "image/gif" -> "gif"
            "video/webm" -> "webm"
            "video/x-matroska" -> "mkv"
            else -> "mp4"
        }
    }
}
