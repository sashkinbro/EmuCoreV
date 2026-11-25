package com.sbro.emucorev.data

import android.content.Context
import androidx.core.content.edit
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

/** A frozen set of one account's pending deltas; [id] stays stable across upload retries (B063). */
data class PendingPlayTimeBatch(
    val id: String,
    val uid: String,
    val entries: List<PlayerPlayTimeDelta>
)

/**
 * Persistent queue of finished play sessions.
 *
 * Every entry belongs to the account that was signed in when it was recorded (B062). Upload is a
 * peek -> commit -> remove cycle: [peekBatch] freezes the open entries of one account into a sealed
 * batch with a stable id, [removeBatch] deletes it only after the server confirmed the commit, and
 * the server ignores a batch id it has already applied. A crash or failure between the commit and
 * the removal therefore re-sends the same batch instead of losing it or counting it twice (B063).
 */
class PlayTimeSyncCacheRepository(context: Context) {

    private val preferences = context.applicationContext.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
    private val mutex = Mutex()

    suspend fun add(entry: PlayerPlayTimeDelta) {
        if (entry.titleId.isBlank() || entry.durationMs <= 0L) return
        mutex.withLock {
            withContext(Dispatchers.IO) {
                val entries = readOpenLocked().toMutableMap()
                mergeInto(entries, entry)
                writeOpenLocked(entries.values.toList())
            }
        }
    }

    /**
     * Returns the sealed batch of [uid] awaiting confirmation, or seals the account's open entries
     * into a new batch. Entries recorded by an older app version carry no owner and are adopted by
     * the first account that syncs. Entries of other accounts are left untouched.
     */
    suspend fun peekBatch(uid: String): PendingPlayTimeBatch? {
        if (uid.isBlank()) return null
        return mutex.withLock {
            withContext(Dispatchers.IO) {
                val sealed = readSealedLocked()
                sealed.firstOrNull { it.uid == uid }?.let { return@withContext it }
                val open = readOpenLocked()
                val mine = open.values.filter { it.uid == uid || it.uid.isBlank() }
                if (mine.isEmpty()) return@withContext null
                val batch = PendingPlayTimeBatch(
                    id = UUID.randomUUID().toString(),
                    uid = uid,
                    entries = mine.map { it.copy(uid = uid) }
                )
                val remaining = open.filterValues { it.uid != uid && it.uid.isNotBlank() }
                writeSealedLocked(sealed + batch)
                writeOpenLocked(remaining.values.toList())
                batch
            }
        }
    }

    /** Removes a batch the server has confirmed. Unknown ids are ignored. */
    suspend fun removeBatch(batchId: String) {
        mutex.withLock {
            withContext(Dispatchers.IO) {
                val sealed = readSealedLocked()
                if (sealed.any { it.id == batchId }) {
                    writeSealedLocked(sealed.filterNot { it.id == batchId })
                }
            }
        }
    }

    /** Drops everything that belongs to [uid] (account deleted). */
    suspend fun discardAccount(uid: String) {
        if (uid.isBlank()) return
        mutex.withLock {
            withContext(Dispatchers.IO) {
                writeSealedLocked(readSealedLocked().filterNot { it.uid == uid })
                writeOpenLocked(readOpenLocked().values.filterNot { it.uid == uid })
            }
        }
    }

    suspend fun getLastCloudSyncAtMs(): Long {
        return mutex.withLock {
            withContext(Dispatchers.IO) {
                preferences.getLong(KEY_LAST_CLOUD_SYNC_AT_MS, 0L)
            }
        }
    }

    suspend fun setLastCloudSyncAtMs(timestampMs: Long) {
        mutex.withLock {
            withContext(Dispatchers.IO) {
                preferences.edit { putLong(KEY_LAST_CLOUD_SYNC_AT_MS, timestampMs) }
            }
        }
    }

    private fun mergeInto(target: MutableMap<String, PlayerPlayTimeDelta>, entry: PlayerPlayTimeDelta) {
        val key = buildCacheKey(entry)
        val existing = target[key]
        target[key] = existing?.copy(
            title = entry.title.ifBlank { existing.title },
            coverArtPath = entry.coverArtPath ?: existing.coverArtPath,
            durationMs = existing.durationMs + entry.durationMs,
            sessionCount = existing.sessionCount + entry.sessionCount,
            lastPlayedAtMs = maxOf(existing.lastPlayedAtMs, entry.lastPlayedAtMs)
        ) ?: entry
    }

    private fun readOpenLocked(): Map<String, PlayerPlayTimeDelta> {
        val raw = preferences.getString(KEY_PENDING_ENTRIES, null).orEmpty()
        if (raw.isBlank()) return emptyMap()
        return runCatching {
            val result = LinkedHashMap<String, PlayerPlayTimeDelta>()
            val array = JSONArray(raw)
            repeat(array.length()) { index ->
                val entry = array.optJSONObject(index)?.toEntry() ?: return@repeat
                mergeInto(result, entry)
            }
            result
        }.getOrDefault(emptyMap())
    }

    private fun writeOpenLocked(entries: Collection<PlayerPlayTimeDelta>) {
        val array = JSONArray()
        entries.filter { it.titleId.isNotBlank() && it.durationMs > 0L }.forEach { array.put(it.toJson()) }
        preferences.edit { putString(KEY_PENDING_ENTRIES, array.toString()) }
    }

    private fun readSealedLocked(): List<PendingPlayTimeBatch> {
        val raw = preferences.getString(KEY_SEALED_BATCHES, null).orEmpty()
        if (raw.isBlank()) return emptyList()
        return runCatching {
            val array = JSONArray(raw)
            buildList {
                repeat(array.length()) { index ->
                    val json = array.optJSONObject(index) ?: return@repeat
                    val id = json.optString("id")
                    val uid = json.optString("uid")
                    if (id.isBlank() || uid.isBlank()) return@repeat
                    val entriesJson = json.optJSONArray("entries") ?: return@repeat
                    val entries = buildList {
                        repeat(entriesJson.length()) { i ->
                            entriesJson.optJSONObject(i)?.toEntry()?.let { add(it.copy(uid = uid)) }
                        }
                    }
                    if (entries.isNotEmpty()) add(PendingPlayTimeBatch(id, uid, entries))
                }
            }
        }.getOrDefault(emptyList())
    }

    private fun writeSealedLocked(batches: List<PendingPlayTimeBatch>) {
        val array = JSONArray()
        batches.forEach { batch ->
            val entries = JSONArray()
            batch.entries.forEach { entries.put(it.toJson()) }
            array.put(JSONObject().put("id", batch.id).put("uid", batch.uid).put("entries", entries))
        }
        preferences.edit { putString(KEY_SEALED_BATCHES, array.toString()) }
    }

    private fun JSONObject.toEntry(): PlayerPlayTimeDelta? {
        val entry = PlayerPlayTimeDelta(
            titleId = optString("titleId"),
            title = optString("title"),
            coverArtPath = optString("cover").takeIf { it.isNotBlank() },
            durationMs = optLong("durationMs"),
            sessionCount = optLong("sessions"),
            lastPlayedAtMs = optLong("lastPlayedAtMs"),
            uid = optString("uid")
        )
        return entry.takeIf { it.titleId.isNotBlank() && it.durationMs > 0L }
    }

    private fun PlayerPlayTimeDelta.toJson(): JSONObject = JSONObject()
        .put("uid", uid)
        .put("titleId", titleId)
        .put("title", title)
        .put("cover", coverArtPath.orEmpty())
        .put("durationMs", durationMs)
        .put("sessions", sessionCount)
        .put("lastPlayedAtMs", lastPlayedAtMs)

    private fun buildCacheKey(entry: PlayerPlayTimeDelta): String = entry.uid + "|" + entry.titleId.uppercase()

    private companion object {
        const val PREFERENCES_NAME = "play_time_sync_cache"
        const val KEY_PENDING_ENTRIES = "pending_entries"
        const val KEY_SEALED_BATCHES = "sealed_batches"
        const val KEY_LAST_CLOUD_SYNC_AT_MS = "last_cloud_sync_at_ms"
    }
}
