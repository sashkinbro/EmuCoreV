package com.sbro.emucorev.data.store

import kotlinx.serialization.KSerializer
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.builtins.ListSerializer
import kotlinx.serialization.descriptors.SerialDescriptor
import kotlinx.serialization.encoding.Decoder
import kotlinx.serialization.encoding.Encoder
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonDecoder

/**
 * `catalog/v1/store.json` — the EmuCore Store catalog, referenced by the optional `manifest.store`
 * file reference of the signed Hub manifest. Format: EmuCoreX-News-Media-Hub/store/README.md.
 * Clients that predate the Store ignore the unknown manifest field.
 */
@Serializable
data class StoreCatalog(
    val formatVersion: Int,
    val releaseId: String,
    @Serializable(with = StoreCategoryListSerializer::class)
    val categories: List<StoreCategory> = emptyList(),
    @Serializable(with = StoreItemListSerializer::class)
    val items: List<StoreItem> = emptyList()
)

/**
 * Decodes a JSON array entry by entry and drops the entries this client cannot read, such as an item or
 * category whose `kind` was added in a newer app version. One unknown entry must not break the whole Store.
 */
internal open class SkippingListSerializer<T>(private val element: KSerializer<T>) : KSerializer<List<T>> {
    private val delegate = ListSerializer(element)
    override val descriptor: SerialDescriptor = delegate.descriptor

    override fun serialize(encoder: Encoder, value: List<T>) = delegate.serialize(encoder, value)

    override fun deserialize(decoder: Decoder): List<T> {
        val input = decoder as? JsonDecoder ?: return delegate.deserialize(decoder)
        val array = input.decodeJsonElement() as? JsonArray ?: throw IllegalArgumentException("Expected a JSON array")
        return array.mapNotNull { entry ->
            // SerializationException is an IllegalArgumentException.
            try {
                input.json.decodeFromJsonElement(element, entry)
            } catch (_: IllegalArgumentException) {
                null
            }
        }
    }
}

internal object StoreCategoryListSerializer : SkippingListSerializer<StoreCategory>(StoreCategory.serializer())

internal object StoreItemListSerializer : SkippingListSerializer<StoreItem>(StoreItem.serializer())

@Serializable
enum class StoreKind {
    @SerialName("theme") THEME,
    @SerialName("wallpaper") WALLPAPER,
    @SerialName("sounds") SOUNDS,
    @SerialName("controls") CONTROLS,
    @SerialName("font") FONT
}

@Serializable
data class StoreCategory(
    val id: String,
    val kind: StoreKind,
    val labels: Map<String, String> = emptyMap()
) {
    fun label(locale: String): String = labels.localizedValue(locale) ?: id
}

@Serializable
data class StoreFile(
    val path: String,
    val sha256: String,
    val bytes: Long,
    val width: Int = 0,
    val height: Int = 0
)

@Serializable
data class StoreItem(
    val id: String,
    val kind: StoreKind,
    val version: Int,
    val requiresPro: Boolean = false,
    val minAppVersionCode: Int = 0,
    val featured: Boolean = false,
    val publishedAt: String = "",
    val categories: List<String> = emptyList(),
    val tags: List<String> = emptyList(),
    val name: Map<String, String> = emptyMap(),
    val description: Map<String, String> = emptyMap(),
    val swatch: List<String> = emptyList(),
    val cover: StoreFile,
    val gallery: List<StoreFile> = emptyList(),
    val packageFile: StoreFile,
    val sizeBytes: Long = 0
) {
    fun displayName(locale: String): String = name.localizedValue(locale) ?: id
    fun displayDescription(locale: String): String = description.localizedValue(locale).orEmpty()

    /** Script coverage of a font item, from the `script:<name>` tags (latin, cyrillic, greek, ...). */
    fun scripts(): List<String> = tagValues("script:")

    /** What a sound pack contains, from the `contains:<music|click|menu|achievement>` tags. */
    fun contents(): List<String> = tagValues("contains:")

    private fun tagValues(prefix: String): List<String> =
        tags.mapNotNull { tag -> tag.takeIf { it.startsWith(prefix) }?.removePrefix(prefix)?.lowercase()?.takeIf(String::isNotBlank) }
            .distinct()
}

/** The font script a user of [language] (ISO 639 code) needs covered; unlisted languages are Latin-script. */
internal fun storeScriptForLanguage(language: String): String = when (language.substringBefore('-').substringBefore('_').lowercase()) {
    "uk", "ru", "be", "bg", "sr", "mk", "kk" -> "cyrillic"
    "ar", "fa", "ur" -> "arabic"
    "hi", "mr", "ne" -> "devanagari"
    "ja", "ko", "zh" -> "cjk"
    "el" -> "greek"
    "vi" -> "vietnamese"
    else -> "latin"
}

/** Catalog resolved against the pinned commit base URL of the sync that produced it. */
data class StoreSnapshot(
    val baseUrl: String,
    val catalogRevision: Long,
    val catalog: StoreCatalog
) {
    fun url(file: StoreFile): String = baseUrl + file.path
}

internal fun Map<String, String>.localizedValue(locale: String): String? {
    val language = locale.substringBefore('-').substringBefore('_').lowercase()
    return this[locale] ?: this[language] ?: this["en"] ?: values.firstOrNull()
}

/**
 * Controller skins are drawn for one console family's buttons. Items tagged `platform:<family>` only show in apps of that
 * family; untagged items show everywhere. This app is a PlayStation app (sibling apps set their own family).
 */
/**
 * Store engine level this client implements. Catalog and theme-package `minAppVersionCode` values are on the
 * EmuCoreX versionCode scale, so they must not be compared with this app's own BuildConfig.VERSION_CODE.
 */
object StoreClient {
    const val ENGINE_VERSION_CODE = 257
}

object StorePlatform {
    const val CURRENT = "playstation"
}

fun StoreItem.fitsThisApp(): Boolean {
    val platforms = tags.filter { it.startsWith("platform:") }
    return platforms.isEmpty() || "platform:${StorePlatform.CURRENT}" in platforms
}

/**
 * This app has no UI click/menu/music sounds, so only sound packs that carry the achievement (trophy) sound are useful;
 * controls items are skins only.
 */
private fun StoreItem.usableInThisApp(): Boolean = when (kind) {
    StoreKind.SOUNDS -> "achievement" in contents()
    else -> true
}

fun StoreSnapshot.forThisApp(): StoreSnapshot =
    copy(catalog = catalog.copy(items = catalog.items.filter { it.fitsThisApp() && it.usableInThisApp() }))
