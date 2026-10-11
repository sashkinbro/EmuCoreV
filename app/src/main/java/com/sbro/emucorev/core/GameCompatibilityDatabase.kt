package com.sbro.emucorev.core

import java.io.InputStream
import java.util.Locale
import javax.xml.parsers.DocumentBuilderFactory
import org.w3c.dom.Element

data class GameGpuRecommendation(
    val backendRenderer: String? = null,
    val memoryMapping: String? = null,
    val useAngle: Boolean? = null,
    val highAccuracy: Boolean? = null
) {
    fun applyTo(base: VitaCoreConfig): VitaCoreConfig = base.copy(
        backendRenderer = backendRenderer ?: base.backendRenderer,
        memoryMapping = memoryMapping ?: base.memoryMapping,
        useAngle = useAngle ?: base.useAngle,
        highAccuracy = highAccuracy ?: base.highAccuracy
    )
}

/** Bundled recommendations, applied BEFORE reading the user's per-game XML. */
class GameCompatibilityDatabase private constructor(private val profiles: List<Profile>) {
    private data class Profile(
        val titleIds: Set<String>, val titleWord: String, val gpu: GameGpuRecommendation
    )

    fun recommendationFor(titleId: String, title: String = ""): GameGpuRecommendation? {
        val id = titleId.trim().uppercase(Locale.ROOT)
        if (id.isEmpty()) return null // Never change the global configuration.
        val name = title.uppercase(Locale.ROOT)
        // An exact title ID takes precedence over a family-name match.
        return (profiles.firstOrNull { id in it.titleIds }
            ?: profiles.firstOrNull { profile ->
                profile.titleWord.isNotEmpty() && Regex(
                    "(?<![A-Z])${Regex.escape(profile.titleWord)}(?![A-Z])"
                ).containsMatchIn(name)
            })?.gpu
    }

    fun applyDefaults(base: VitaCoreConfig, titleId: String, title: String = ""): VitaCoreConfig =
        recommendationFor(titleId, title)?.applyTo(base) ?: base

    companion object {
        const val ASSET_PATH = "compatibility/game-db.xml"
        val EMPTY = GameCompatibilityDatabase(emptyList())

        fun parse(input: InputStream): GameCompatibilityDatabase {
            val factory = DocumentBuilderFactory.newInstance().apply {
                isExpandEntityReferences = false
            }
            // Android's Harmony parser does not implement Xerces feature URIs.
            // This is a bundled APK asset, not downloaded/user-provided XML.
            val builder = factory.newDocumentBuilder().apply {
                setEntityResolver { _, _ -> org.xml.sax.InputSource(java.io.StringReader("")) }
            }
            val root = builder.parse(input).documentElement
            if (root == null || root.tagName != "game-db" || root.getAttribute("version") != "1") return EMPTY
            val profiles = mutableListOf<Profile>()
            val nodes = root.getElementsByTagName("profile")
            for (index in 0 until nodes.length) {
                val entry = nodes.item(index) as? Element ?: continue
                if (entry.parentNode != root) continue
                val gpu = entry.getElementsByTagName("gpu").item(0) as? Element ?: continue
                if (gpu.parentNode != entry) continue
                val hasLegacySettings = listOf("backend-renderer", "memory-mapping", "use-angle")
                    .any(gpu::hasAttribute)
                val renderer = gpu.getAttribute("backend-renderer").takeIf { gpu.hasAttribute("backend-renderer") }
                val mapping = gpu.getAttribute("memory-mapping").takeIf { gpu.hasAttribute("memory-mapping") }
                val angle = gpu.getAttribute("use-angle").toBooleanStrictOrNull()
                // Existing renderer profiles still require the complete validated tuple.
                if (hasLegacySettings && (renderer !in listOf("Vulkan", "OpenGL") || mapping !in listOf(
                    "disabled", "double-buffer", "external-host", "page-table", "native-buffer"
                ) || angle == null)) continue
                val accuracy = gpu.getAttribute("high-accuracy").toBooleanStrictOrNull()
                if (gpu.hasAttribute("high-accuracy") && accuracy == null) continue
                if (!hasLegacySettings && accuracy == null) continue
                val ids = entry.getElementsByTagName("title-id")
                val titleIds = (0 until ids.length).mapNotNull {
                    ids.item(it).takeIf { node -> node.parentNode == entry }
                        ?.textContent?.trim()?.uppercase(Locale.ROOT)
                }.filter(String::isNotEmpty).toSet()
                profiles += Profile(titleIds, entry.getAttribute("title-word").trim().uppercase(Locale.ROOT),
                    GameGpuRecommendation(renderer, mapping, angle, accuracy))
            }
            return GameCompatibilityDatabase(profiles)
        }
    }
}
