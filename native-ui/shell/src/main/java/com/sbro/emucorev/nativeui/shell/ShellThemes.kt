package com.sbro.emucorev.nativeui.shell

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import org.xmlpull.v1.XmlPullParser
import org.xmlpull.v1.XmlPullParserFactory
import java.io.File
import java.util.UUID
import java.util.zip.ZipInputStream

/**
 * Custom themes in the PS Vita theme package layout: a theme.xml with
 * HomeProperty (per-page m_bgParam backgrounds), StartScreenProperty
 * (m_filePath, m_dateColor) and InfomationBarProperty (m_barColor), plus the
 * referenced images. Installed copies live in app-private storage; the
 * Home/Start screen photo choices of Settings are stored the same way.
 */
class ShellThemes(private val context: Context) {
    data class Theme(
        val id: String,
        val title: String,
        val homeBackgrounds: List<File>,
        val startScreen: File?,
        /** ARGB, or null for the default. */
        val dateColor: Int?,
        val barColor: Int?,
    )

    private val root = File(context.noBackupFilesDir, "native-themes").apply { mkdirs() }
    private val prefs = context.getSharedPreferences("native_shell_settings", Context.MODE_PRIVATE)

    /** Selected custom theme id, or null for a built-in wave theme. */
    var activeTheme: String?
        get() = prefs.getString("custom_theme", null)
        set(value) = prefs.edit().putString("custom_theme", value).apply()

    val homePhoto: File get() = File(root, "home-photo.img")
    val startPhoto: File get() = File(root, "start-photo.img")

    fun installed(): List<Theme> = root.listFiles().orEmpty()
        .filter { it.isDirectory && File(it, "theme.xml").isFile }
        .mapNotNull { runCatching { parse(it) }.getOrNull() }
        .sortedBy { it.title.lowercase() }

    fun active(): Theme? = activeTheme?.let { id -> installed().firstOrNull { it.id == id } }

    /** Installs a theme ZIP (theme.xml at its root or in one top folder). */
    fun import(uri: Uri): Theme {
        val id = UUID.randomUUID().toString()
        val staging = File(root, ".staging-$id")
        staging.mkdirs()
        try {
            var total = 0L
            context.contentResolver.openInputStream(uri).use { raw ->
                ZipInputStream(requireNotNull(raw) { "Cannot open theme" }).use { zip ->
                    val base = staging.canonicalFile
                    while (true) {
                        val entry = zip.nextEntry ?: break
                        val target = File(staging, entry.name).canonicalFile
                        require(target.path.startsWith(base.path + File.separator)) { "Invalid theme entry" }
                        if (entry.isDirectory) { target.mkdirs(); continue }
                        target.parentFile?.mkdirs()
                        target.outputStream().use { out ->
                            val buffer = ByteArray(65536)
                            while (true) {
                                val n = zip.read(buffer)
                                if (n < 0) break
                                total += n
                                require(total <= MAX_THEME_BYTES) { "Theme is too large" }
                                out.write(buffer, 0, n)
                            }
                        }
                    }
                }
            }
            val xml = staging.walkTopDown().maxDepth(3).firstOrNull { it.name.equals("theme.xml", true) }
                ?: error("theme.xml is missing")
            val dir = File(root, id)
            check(xml.parentFile!!.renameTo(dir)) { "Cannot install theme" }
            return parse(dir)
        } finally {
            staging.deleteRecursively()
        }
    }

    fun delete(id: String) {
        File(root, id).takeIf { it.parentFile == root }?.deleteRecursively()
        if (activeTheme == id) activeTheme = null
    }

    /** Copies a picked image (Home / Start screen background). */
    fun savePhoto(uri: Uri, target: File) {
        context.contentResolver.openInputStream(uri).use { input ->
            requireNotNull(input) { "Cannot open image" }
            target.outputStream().use { input.copyTo(it) }
        }
        requireNotNull(decode(target, 64)) { "Unsupported image" }
    }

    private fun parse(dir: File): Theme {
        var title = dir.name
        val homeBackgrounds = ArrayList<File>()
        var startScreen: File? = null
        var dateColor: Int? = null
        var barColor: Int? = null
        val stack = ArrayList<String>()
        val parser = XmlPullParserFactory.newInstance().newPullParser()
        File(dir, "theme.xml").inputStream().use { input ->
            parser.setInput(input, null)
            while (parser.next() != XmlPullParser.END_DOCUMENT) {
                when (parser.eventType) {
                    XmlPullParser.START_TAG -> stack += parser.name
                    XmlPullParser.END_TAG -> stack.removeAt(stack.lastIndex)
                    XmlPullParser.TEXT -> {
                        val value = parser.text.trim()
                        if (value.isEmpty() || stack.isEmpty()) continue
                        val tag = stack.last()
                        fun file() = File(dir, value.replace('\\', '/')).canonicalFile
                            .takeIf { it.path.startsWith(dir.canonicalPath) && it.isFile }
                        when {
                            tag == "m_title" && "HomeProperty" !in stack -> title = value
                            tag == "m_imageFilePath" && "HomeProperty" in stack -> file()?.let(homeBackgrounds::add)
                            tag == "m_filePath" && "StartScreenProperty" in stack -> startScreen = file()
                            tag == "m_dateColor" -> dateColor = color(value)
                            tag == "m_barColor" -> barColor = color(value)
                        }
                    }
                }
            }
        }
        return Theme(dir.name, title, homeBackgrounds.take(10), startScreen, dateColor, barColor)
    }

    /** Vita theme colours are written as AARRGGBB hex. */
    private fun color(value: String): Int? =
        value.removePrefix("0x").removePrefix("#").toLongOrNull(16)?.toInt()

    companion object {
        private const val MAX_THEME_BYTES = 64L * 1024 * 1024

        fun decode(file: File, maxSide: Int = 1920): Bitmap? {
            if (!file.isFile) return null
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(file.path, bounds)
            if (bounds.outWidth <= 0) return null
            var sample = 1
            while (bounds.outWidth / (sample * 2) >= maxSide || bounds.outHeight / (sample * 2) >= maxSide) sample *= 2
            return BitmapFactory.decodeFile(file.path, BitmapFactory.Options().apply { inSampleSize = sample })
        }
    }
}
