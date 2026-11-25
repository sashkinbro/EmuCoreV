package com.sbro.emucorev.data.store

import android.content.Context
import com.sbro.emucorev.data.theme.ThemePackageRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.io.File

data class InstalledStoreItem(
    val id: String,
    val kind: StoreKind,
    val version: Int,
    val directory: File
)

/**
 * What is installed from the Store, discovered from disk: wallpapers, sounds and controls under
 * `filesDir/store/<kind>/<id>/`, themes through [ThemePackageRepository]. Ids are unique across kinds.
 */
class StoreInstalls private constructor(context: Context) {
    private val appContext = context.applicationContext
    private val themes = ThemePackageRepository.getInstance(appContext)
    private val _installed = MutableStateFlow<Map<String, InstalledStoreItem>>(emptyMap())

    /** Installed items by id. Call [refresh] after changing files outside this class. */
    val installed: StateFlow<Map<String, InstalledStoreItem>> = _installed.asStateFlow()

    init {
        refresh()
    }

    fun find(id: String?): InstalledStoreItem? = id?.let { _installed.value[it] }

    @Synchronized
    fun refresh() {
        val found = LinkedHashMap<String, InstalledStoreItem>()
        themes.reload()
        themes.installed.value.forEach { pack ->
            found[pack.id] = InstalledStoreItem(pack.id, StoreKind.THEME, pack.spec.version, pack.directory)
        }
        scan(StoreKind.WALLPAPER, WALLPAPER_JSON, found)
        scan(StoreKind.SOUNDS, SOUNDS_JSON, found)
        scan(StoreKind.CONTROLS, CONTROLS_JSON, found)
        scan(StoreKind.FONT, FONT_JSON, found)
        _installed.value = found
    }

    /** Deletes the installed files of [id]. Clearing applied preferences is [StoreApplier.remove]'s job. */
    suspend fun remove(id: String): Boolean = withContext(Dispatchers.IO) {
        val item = find(id) ?: return@withContext false
        val removed = if (item.kind == StoreKind.THEME) themes.remove(id) else item.directory.deleteRecursively()
        refresh()
        removed
    }

    private fun scan(kind: StoreKind, manifestName: String, into: MutableMap<String, InstalledStoreItem>) {
        kindDirectory(appContext, kind).listFiles().orEmpty()
            .filter { it.isDirectory && !it.name.startsWith(".") }
            .forEach { directory ->
                val header = runCatching {
                    JSON.decodeFromString<Header>(File(directory, manifestName).readText())
                }.getOrNull() ?: return@forEach
                if (header.id != directory.name) return@forEach
                into[header.id] = InstalledStoreItem(header.id, kind, header.version, directory)
            }
    }

    @Serializable
    private data class Header(val id: String = "", val version: Int = 0)

    companion object {
        internal const val WALLPAPER_JSON = "wallpaper.json"
        internal const val SOUNDS_JSON = "sounds.json"
        internal const val CONTROLS_JSON = "controls.json"
        internal const val FONT_JSON = "font.json"
        internal val ID_PATTERN = Regex("[a-z0-9-]{3,48}")
        internal val JSON = Json {
            ignoreUnknownKeys = true
            explicitNulls = false
        }

        /** `filesDir/store/<wallpapers|sounds|controls|fonts>`; themes live in ThemePackageRepository's directory. */
        internal fun kindDirectory(context: Context, kind: StoreKind): File {
            val name = when (kind) {
                StoreKind.WALLPAPER -> "wallpapers"
                StoreKind.SOUNDS -> "sounds"
                StoreKind.CONTROLS -> "controls"
                StoreKind.FONT -> "fonts"
                StoreKind.THEME -> "themes"
            }
            return File(File(context.applicationContext.filesDir, "store"), name)
        }

        @Volatile private var instance: StoreInstalls? = null

        fun getInstance(context: Context): StoreInstalls = instance ?: synchronized(this) {
            instance ?: StoreInstalls(context).also { instance = it }
        }
    }
}

/** Resolves the files of an installed Store sound pack; used by UiSoundManager. */
object StoreSoundPacks {
    val KEYS = listOf("click", "menu_open", "menu_close", "menu_select", "music", "achievement")

    /** [key] is one of [KEYS]. Null when the pack or that sound is not installed. */
    fun file(context: Context, packId: String, key: String): File? {
        if (!StoreInstalls.ID_PATTERN.matches(packId) || key !in KEYS) return null
        return fileIn(File(StoreInstalls.kindDirectory(context, StoreKind.SOUNDS), packId), key)
    }

    /** The [key] sound inside an unpacked pack [directory] (installed or preview); null when absent. */
    fun fileIn(directory: File, key: String): File? {
        if (key !in KEYS) return null
        return listOf("ogg", "wav").map { File(directory, "$key.$it") }.firstOrNull { it.isFile && it.length() > 0L }
    }

    /** Which parts the installed pack contains: any of "music", "click", "menu", "achievement". */
    fun contents(context: Context, packId: String): Set<String> = buildSet {
        if (file(context, packId, "music") != null) add("music")
        if (file(context, packId, "click") != null) add("click")
        if (listOf("menu_open", "menu_close", "menu_select").any { file(context, packId, it) != null }) add("menu")
        if (file(context, packId, "achievement") != null) add("achievement")
    }
}

/** Resolves the font file of an installed Store font. */
object StoreFontPacks {
    fun file(context: Context, fontId: String): File? {
        if (!StoreInstalls.ID_PATTERN.matches(fontId)) return null
        val directory = File(StoreInstalls.kindDirectory(context, StoreKind.FONT), fontId)
        return listOf("font.ttf", "font.otf").map { File(directory, it) }.firstOrNull { it.isFile && it.length() > 0L }
    }
}
