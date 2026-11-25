package com.sbro.emucorev.data.theme

import android.content.Context
import android.graphics.BitmapFactory
import android.graphics.Typeface
import android.net.Uri
import com.sbro.emucorev.BuildConfig
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream
import java.util.zip.ZipInputStream
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext

data class InstalledThemePackage(
    val spec: ThemePackageSpec,
    val directory: File,
    val fontFile: File?,
    val backgroundFile: File?
) {
    val id: String get() = spec.id
    val isSupported: Boolean
        get() = spec.formatVersion <= ThemePackageSpec.SUPPORTED_FORMAT_VERSION &&
            spec.minAppVersionCode <= com.sbro.emucorev.data.store.StoreClient.ENGINE_VERSION_CODE
}

class ThemePackageException(message: String) : IllegalArgumentException(message)

/**
 * Installs, lists and removes `.ecxtheme` packages under `filesDir/theme-packages/<id>/`.
 * Every archive is fully validated in memory before anything is written.
 */
class ThemePackageRepository private constructor(context: Context) {
    private val root = File(context.applicationContext.filesDir, DIRECTORY_NAME)
    private val _installed = MutableStateFlow<List<InstalledThemePackage>>(emptyList())
    val installed: StateFlow<List<InstalledThemePackage>> = _installed.asStateFlow()

    @Volatile
    private var loaded = false

    fun installedSync(): List<InstalledThemePackage> {
        if (!loaded) reload()
        return _installed.value
    }

    fun find(id: String?): InstalledThemePackage? {
        if (id.isNullOrBlank()) return null
        return installedSync().firstOrNull { it.id == id }
    }

    suspend fun install(uri: Uri, context: Context): Result<InstalledThemePackage> = withContext(Dispatchers.IO) {
        runCatching {
            val input = context.contentResolver.openInputStream(uri)
                ?: throw ThemePackageException("Unable to open theme package")
            input.use { installBlocking(it) }
        }
    }

    suspend fun install(bytes: ByteArray): Result<InstalledThemePackage> = withContext(Dispatchers.IO) {
        runCatching { installBlocking(bytes.inputStream()) }
    }

    suspend fun remove(id: String): Boolean = withContext(Dispatchers.IO) {
        val target = find(id) ?: return@withContext false
        val removed = target.directory.deleteRecursively()
        reload()
        removed
    }

    @Synchronized
    private fun installBlocking(input: InputStream): InstalledThemePackage {
        val entries = readArchive(input)
        val themeBytes = entries[THEME_JSON] ?: throw ThemePackageException("theme.json is missing")
        val spec = runCatching { ThemePackageSpec.decode(themeBytes.decodeToString()) }
            .getOrElse { throw ThemePackageException(it.message ?: "Invalid theme.json") }

        val existing = find(spec.id)
        if (existing != null && existing.spec.version > spec.version) {
            throw ThemePackageException("A newer version of this theme is already installed")
        }

        val fontEntry = FONT_NAMES.firstOrNull(entries::containsKey)
        val backgroundEntry = BACKGROUND_NAMES.firstOrNull(entries::containsKey)
        if (spec.typography.font.equals("package", ignoreCase = true) && fontEntry == null) {
            throw ThemePackageException("The theme uses a package font but has no font file")
        }
        backgroundEntry?.let { name -> validateImage(entries.getValue(name)) }

        root.mkdirs()
        val staging = File(root, ".staging-${spec.id}")
        staging.deleteRecursively()
        check(staging.mkdirs()) { "Unable to prepare theme directory" }
        try {
            entries.forEach { (name, bytes) -> File(staging, name).writeBytes(bytes) }
            fontEntry?.let { name -> validateFont(File(staging, name)) }
            val target = File(root, spec.id)
            target.deleteRecursively()
            check(staging.renameTo(target)) { "Unable to save theme" }
        } finally {
            staging.deleteRecursively()
        }
        reload()
        return find(spec.id) ?: error("Installed theme is not readable")
    }

    @Synchronized
    fun reload() {
        val packages = root.listFiles()
            .orEmpty()
            .filter { it.isDirectory && !it.name.startsWith(".") }
            .mapNotNull(::readInstalled)
            .sortedBy { it.spec.displayName("en").lowercase() }
        _installed.value = packages
        loaded = true
    }

    private fun readInstalled(directory: File): InstalledThemePackage? = runCatching {
        val spec = ThemePackageSpec.decode(File(directory, THEME_JSON).readText())
        if (spec.id != directory.name) return null
        InstalledThemePackage(
            spec = spec,
            directory = directory,
            fontFile = FONT_NAMES.map { File(directory, it) }.firstOrNull(File::isFile),
            backgroundFile = BACKGROUND_NAMES.map { File(directory, it) }.firstOrNull(File::isFile)
        )
    }.getOrNull()

    private fun readArchive(input: InputStream): Map<String, ByteArray> {
        val entries = LinkedHashMap<String, ByteArray>()
        var compressedGuard = 0L
        var totalUncompressed = 0L
        ZipInputStream(CountingInputStream(input) { compressedGuard = it }).use { zip ->
            while (true) {
                val entry = zip.nextEntry ?: break
                val name = entry.name
                if (entry.isDirectory || name !in ALLOWED_NAMES) {
                    throw ThemePackageException("Unexpected file in theme package: ${name.take(64)}")
                }
                if (entries.containsKey(name)) throw ThemePackageException("Duplicate file in theme package")
                val limit = LIMITS.getValue(name)
                val bytes = readLimited(zip, limit)
                totalUncompressed += bytes.size
                if (totalUncompressed > MAX_UNCOMPRESSED_BYTES) throw ThemePackageException("Theme package is too large")
                if (compressedGuard > MAX_ARCHIVE_BYTES) throw ThemePackageException("Theme package is too large")
                entries[name] = bytes
            }
        }
        if (entries.isEmpty()) throw ThemePackageException("Not a theme package")
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
            if (total > limit) throw ThemePackageException("A file in the theme package is too large")
            output.write(buffer, 0, read)
        }
        return output.toByteArray()
    }

    private fun validateImage(bytes: ByteArray) {
        val options = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(bytes, 0, bytes.size, options)
        if (options.outWidth <= 0 || options.outHeight <= 0 ||
            options.outWidth > MAX_IMAGE_SIDE || options.outHeight > MAX_IMAGE_SIDE
        ) {
            throw ThemePackageException("Invalid theme background image")
        }
    }

    private fun validateFont(file: File) {
        runCatching { Typeface.createFromFile(file) }
            .getOrNull()
            ?: throw ThemePackageException("Invalid theme font")
    }

    private class CountingInputStream(
        private val delegate: InputStream,
        private val onCount: (Long) -> Unit
    ) : InputStream() {
        private var count = 0L
        override fun read(): Int = delegate.read().also { if (it >= 0) onCount(++count) }
        override fun read(b: ByteArray, off: Int, len: Int): Int =
            delegate.read(b, off, len).also { if (it > 0) { count += it; onCount(count) } }
        override fun close() = delegate.close()
    }

    companion object {
        const val FILE_EXTENSION = "ecxtheme"
        private const val DIRECTORY_NAME = "theme-packages"
        private const val THEME_JSON = "theme.json"
        private val FONT_NAMES = listOf("font.ttf", "font.otf")
        private val BACKGROUND_NAMES = listOf("background.webp", "background.jpg", "background.png")
        private val ALLOWED_NAMES = setOf(THEME_JSON) + FONT_NAMES + BACKGROUND_NAMES
        private val LIMITS: Map<String, Long> = buildMap {
            put(THEME_JSON, 64L * 1024)
            FONT_NAMES.forEach { put(it, 1024L * 1024) }
            BACKGROUND_NAMES.forEach { put(it, 2L * 1024 * 1024) }
        }
        private const val MAX_ARCHIVE_BYTES = 4L * 1024 * 1024
        private const val MAX_UNCOMPRESSED_BYTES = 6L * 1024 * 1024
        private const val MAX_IMAGE_SIDE = 2560

        @Volatile
        private var instance: ThemePackageRepository? = null

        fun getInstance(context: Context): ThemePackageRepository =
            instance ?: synchronized(this) {
                instance ?: ThemePackageRepository(context).also { instance = it }
            }
    }
}
