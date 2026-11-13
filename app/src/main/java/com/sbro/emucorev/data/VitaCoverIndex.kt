package com.sbro.emucorev.data

import java.net.URI
import java.util.Locale
import org.json.JSONObject

enum class VitaCoverStyle { FLAT, THREE_D }

data class VitaCoverAsset(
    val igdbId: Long,
    val style: VitaCoverStyle,
    val revision: String,
    val relativePath: String,
    val sha256: String
) {
    init {
        require(igdbId > 0 && revision.matches(Regex("[a-f0-9]{40}")))
        require(sha256.matches(Regex("[a-f0-9]{64}")))
        require(relativePath == "covers/${if (style == VitaCoverStyle.THREE_D) "3d/" else ""}$igdbId.webp")
    }
    val url: String get() = "https://raw.githubusercontent.com/sashkinbro/EmuCoreV-Covers/$revision/$relativePath"
    val cacheKey: String get() = "$revision/${style.name}/$igdbId-$sha256.webp"
}

/** Immutable, reviewed identity snapshot. No name matching or IGDB API access. */
class VitaCoverIndex private constructor(
    private val games: Map<Long, Game> = emptyMap(),
    private val serials: Map<String, Long> = emptyMap(),
    private val imageIds: Map<String, List<Long>> = emptyMap()
) {
    private data class Game(val flat: VitaCoverAsset, val threeD: VitaCoverAsset, val imageId: String)

    fun find(titleId: String? = null, igdbId: Long? = null, candidateUrl: String? = null,
             style: VitaCoverStyle = VitaCoverStyle.THREE_D): VitaCoverAsset? {
        val serialId = if (titleId != null) {
            val serial = normalizeTitleId(titleId) ?: return null
            serials[serial] ?: return null
        } else null
        if (igdbId != null && (igdbId <= 0 || igdbId !in games)) return null
        val candidateId = if (!candidateUrl.isNullOrBlank()) {
            val image = sourceImageId(candidateUrl)
            when {
                image != null -> imageIds[image]?.singleOrNull() ?: return null
                isManagedRemoteUrl(candidateUrl) -> repoGameId(candidateUrl)?.takeIf { it in games } ?: return null
                else -> null
            }
        } else null
        val identities = listOfNotNull(serialId, igdbId, candidateId).distinct()
        val id = identities.singleOrNull() ?: return null
        return games[id]?.let { if (style == VitaCoverStyle.THREE_D) it.threeD else it.flat }
    }

    companion object {
        private const val REPO_PATH = "/sashkinbro/EmuCoreV-Covers/"

        fun normalizeTitleId(value: String): String? = value.uppercase(Locale.ROOT)
            .replace(Regex("[-_.\\s]"), "").takeIf { it.matches(Regex("[A-Z]{4}[0-9]{5}")) }

        /** Only a known source host or our repository is classified as managed remote art. */
        fun isManagedRemoteUrl(value: String): Boolean {
            val uri = uri(value) ?: return false
            return uri.host.equals("images.igdb.com", true) ||
                (uri.host.equals("raw.githubusercontent.com", true) && uri.path.startsWith(REPO_PATH))
        }

        private fun uri(value: String): URI? = runCatching {
            URI(if (value.startsWith("//")) "https:$value" else value)
                .takeIf { it.scheme == "https" && it.userInfo == null && it.port == -1 &&
                    it.rawQuery == null && it.rawFragment == null && it.rawPath == it.path }
        }.getOrNull()

        private fun sourceImageId(value: String): String? {
            val uri = uri(value) ?: return null
            if (!uri.host.equals("images.igdb.com", true)) return null
            return Regex("/igdb/image/upload/t_[A-Za-z0-9_]+/([A-Za-z0-9_-]{1,128})\\.(?:jpg|png|webp)")
                .matchEntire(uri.path)?.groupValues?.get(1)
        }

        private fun repoGameId(value: String): Long? {
            val uri = uri(value) ?: return null
            if (!uri.host.equals("raw.githubusercontent.com", true)) return null
            return Regex("${REPO_PATH}[a-f0-9]{40}/covers/(?:3d/)?([1-9][0-9]*)\\.webp")
                .matchEntire(uri.path)?.groupValues?.get(1)?.toLongOrNull()
        }

        /** Malformed snapshots fail closed; malformed records cannot authorize a download. */
        fun fromJson(json: String): VitaCoverIndex = runCatching {
            require(json.length <= 32 * 1024 * 1024)
            val root = JSONObject(json)
            require(root.opt("schema_version") is Number && root.optInt("schema_version") == 2)
            val revision = root.getString("asset_revision").lowercase(Locale.ROOT)
            require(revision.matches(Regex("[a-f0-9]{40}")))
            val gameJson = root.getJSONObject("games")
            val serialJson = root.getJSONObject("serials")
            require(gameJson.length() <= 20_000 && serialJson.length() <= 200_000)
            val games = mutableMapOf<Long, Game>()
            gameJson.keys().forEach { key ->
                val id = key.toLongOrNull()?.takeIf { it > 0 && it.toString() == key } ?: return@forEach
                val game = gameJson.optJSONObject(key) ?: return@forEach
                runCatching {
                    val image = game.getString("source_image_id")
                    require(image.matches(Regex("[A-Za-z0-9_-]{1,128}")))
                    require(sourceImageId(game.getString("source_url")) == image)
                    val flat = VitaCoverAsset(id, VitaCoverStyle.FLAT, revision,
                        game.getString("path"), game.getString("sha256").lowercase(Locale.ROOT))
                    val threeD = VitaCoverAsset(id, VitaCoverStyle.THREE_D, revision,
                        game.getString("path_3d"), game.getString("sha256_3d").lowercase(Locale.ROOT))
                    Game(flat, threeD, image)
                }.getOrNull()?.let { games[id] = it }
            }
            val serials = mutableMapOf<String, Long>()
            val conflicts = mutableSetOf<String>()
            serialJson.keys().forEach { key ->
                val serial = normalizeTitleId(key) ?: return@forEach
                val value = serialJson.opt(key) as? String
                val id = value?.toLongOrNull()?.takeIf { it > 0 && it.toString() == value && it in games }
                if (id == null || serials[serial]?.let { it != id } == true) conflicts += serial
                else serials[serial] = id
            }
            conflicts.forEach(serials::remove)
            VitaCoverIndex(games.toMap(), serials.toMap(), games.entries.groupBy({ it.value.imageId }, { it.key }))
        }.getOrElse { VitaCoverIndex() }
    }
}
