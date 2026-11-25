package com.sbro.emucorev.data

import java.security.MessageDigest

/** The slice of an existing `users/{uid}` document that [ProfileDocLogic.ensureProfilePatches] looks at. */
internal data class ExistingProfileDoc(
    val exists: Boolean = false,
    val displayName: String? = null,
    val photoURL: String? = null,
    val totalPlayTimeMs: Long = 0L,
    val gamesPlayed: Long? = null,
    val gamesMapSize: Int = 0,
    val lastPlayedTitle: String? = null,
    val lastPlayedAtMs: Long? = null,
    val profileAccent: String? = null,
    val favoriteGameKeys: List<String> = emptyList()
)

internal data class EnsureProfilePatches(
    val user: Map<String, Any?>,
    val publicProfile: Map<String, Any?>,
    val leaderboard: Map<String, Any?>
)

/**
 * Pure document-shaping rules of the player profile, kept free of Firebase types so they can be
 * unit tested. Field names are the Firestore field names.
 */
internal object ProfileDocLogic {
    /**
     * Largest play-time increase a single upload may carry. PROVISIONAL client-side bound; the
     * proposed security rules (docs/firestore.rules.proposal) use the same figure (B067).
     */
    const val MAX_BATCH_PLAY_TIME_MS: Long = 30L * 24L * 60L * 60L * 1000L

    /** PROVISIONAL: largest session count a single game entry of one upload may carry. */
    const val MAX_ENTRY_SESSIONS: Long = 10_000L

    /** The users/{uid}.games map is only a bounded cache of the most played games (B064). */
    const val MAX_GAMES_MAP_ENTRIES: Int = 50

    /** Applied batch ids remembered on the user document so a re-sent batch is not counted twice. */
    const val MAX_REMEMBERED_BATCH_IDS: Int = 20

    const val DEFAULT_PROFILE_ACCENT = "gold"

    /** Caps each entry and the whole upload to [MAX_BATCH_PLAY_TIME_MS]; non-positive entries are dropped. */
    fun boundDeltas(entries: List<PlayerPlayTimeDelta>): List<PlayerPlayTimeDelta> {
        var budget = MAX_BATCH_PLAY_TIME_MS
        val result = ArrayList<PlayerPlayTimeDelta>(entries.size)
        for (entry in entries) {
            if (entry.titleId.isBlank() || entry.durationMs <= 0L) continue
            val duration = minOf(entry.durationMs, budget)
            if (duration <= 0L) break
            budget -= duration
            result += entry.copy(
                durationMs = duration,
                sessionCount = entry.sessionCount.coerceIn(0L, MAX_ENTRY_SESSIONS)
            )
        }
        return result
    }

    /** Merges entries of the same game and pairs each with its Firestore game key. */
    fun groupByGame(entries: List<PlayerPlayTimeDelta>): List<Pair<String, PlayerPlayTimeDelta>> {
        return entries
            .filter { it.titleId.isNotBlank() && it.durationMs > 0L }
            .groupBy { buildGameKey(it.titleId) }
            .map { (gameKey, gameEntries) ->
                val latest = gameEntries.maxBy { it.lastPlayedAtMs }
                gameKey to latest.copy(
                    durationMs = gameEntries.sumOf { it.durationMs },
                    sessionCount = gameEntries.sumOf { it.sessionCount }
                )
            }
    }

    fun buildGameKey(titleId: String): String {
        val key = titleId
            .trim()
            .uppercase()
            .replace(Regex("[^A-Z0-9_-]"), "_")
            .takeIf { it.isNotBlank() }
        return key ?: "T_${sha256(titleId).take(32)}"
    }

    fun sha256(value: String): String {
        val digest = MessageDigest.getInstance("SHA-256")
        return digest.digest(value.toByteArray()).joinToString("") { "%02x".format(it) }
    }

    /**
     * A stored name is kept unless [forceIncoming] (an explicit user choice such as the sign-up form):
     * background sign-in sync must not overwrite it with a provider fallback (B060, B065).
     */
    fun resolveDisplayName(existing: String?, incoming: String, forceIncoming: Boolean): String {
        if (forceIncoming) return incoming
        return existing?.takeIf { it.isNotBlank() } ?: incoming
    }

    /** True when [batchId] was already applied to the account. */
    fun isBatchApplied(rememberedBatchIds: List<String>, batchId: String): Boolean = batchId in rememberedBatchIds

    fun rememberBatch(rememberedBatchIds: List<String>, batchId: String): List<String> {
        return (rememberedBatchIds.filterNot { it == batchId } + batchId).takeLast(MAX_REMEMBERED_BATCH_IDS)
    }

    /**
     * Fields to merge into users/{uid}.games: the games just played (when they stay within [cap]) and
     * [deleteMarker] for every previously stored game that falls out of the top [cap] by play time.
     * Unchanged games that stay are not rewritten.
     */
    fun gamesMapPatch(
        existing: Map<String, Map<String, Any?>>,
        changed: Map<String, Map<String, Any?>>,
        cap: Int,
        deleteMarker: Any
    ): Map<String, Any?> {
        val merged = LinkedHashMap<String, Map<String, Any?>>(existing)
        merged.putAll(changed)
        val kept = merged.entries
            .sortedWith(compareByDescending<Map.Entry<String, Map<String, Any?>>> { numberOf(it.value["ms"]) }.thenBy { it.key })
            .take(cap)
            .map { it.key }
            .toSet()
        val patch = LinkedHashMap<String, Any?>()
        changed.forEach { (key, value) -> if (key in kept) patch[key] = value }
        existing.keys.forEach { key -> if (key !in kept) patch[key] = deleteMarker }
        return patch
    }

    fun numberOf(value: Any?): Long = (value as? Number)?.toLong() ?: 0L

    /**
     * The sign-in profile bootstrap (B060, B065, B064): keeps what is already stored, never rewrites the
     * games map and never resets last-played data that the document does not carry.
     */
    fun ensureProfilePatches(
        uid: String,
        email: String?,
        incomingName: String,
        photoURL: String?,
        forceName: Boolean,
        existing: ExistingProfileDoc,
        playerTag: String,
        serverTimestamp: Any
    ): EnsureProfilePatches {
        val displayName = resolveDisplayName(existing.displayName.takeIf { existing.exists }, incomingName, forceName)
        val searchName = normalizeSearchName(displayName)
        val photo = photoURL?.takeIf { it.isNotBlank() } ?: existing.photoURL.orEmpty()
        val gamesPlayed = if (existing.exists) existing.gamesPlayed ?: existing.gamesMapSize.toLong() else 0L
        val accent = existing.profileAccent?.takeIf { it in PROFILE_ACCENTS } ?: DEFAULT_PROFILE_ACCENT
        val favorites = existing.favoriteGameKeys.distinct().take(MAX_FAVORITE_GAMES)

        val common = linkedMapOf<String, Any?>(
            "profileSchemaVersion" to PROFILE_SCHEMA_VERSION,
            "uid" to uid,
            "displayName" to displayName,
            "searchName" to searchName,
            "playerTag" to playerTag,
            "photoURL" to photo,
            "totalPlayTimeMs" to existing.totalPlayTimeMs,
            "gamesPlayed" to gamesPlayed,
            "profileAccent" to accent,
            "updatedAt" to serverTimestamp
        )
        val lastTitle = existing.lastPlayedTitle?.takeIf { it.isNotBlank() }
        val user = LinkedHashMap(common).apply {
            put("email", email ?: "")
            put("favoriteGameKeys", favorites)
            if (!existing.exists) put("createdAt", serverTimestamp)
        }
        val publicProfile = LinkedHashMap(common).apply {
            put("favoriteGameKeys", favorites)
            lastTitle?.let { put("lastPlayedTitle", it) }
            existing.lastPlayedAtMs?.let { put("lastPlayedAtMs", it) }
        }
        val leaderboard = LinkedHashMap(common).apply {
            lastTitle?.let { put("lastPlayedTitle", it) }
        }
        return EnsureProfilePatches(user, publicProfile, leaderboard)
    }

    fun normalizeSearchName(value: String): String {
        return value.trim()
            .lowercase(java.util.Locale.ROOT)
            .replace(Regex("\\s+"), " ")
            .truncateGraphemes()
    }

    const val MAX_FAVORITE_GAMES = 3
    const val PROFILE_SCHEMA_VERSION = 2
    val PROFILE_ACCENTS = setOf("gold", "crimson", "blue", "violet", "emerald")
}
