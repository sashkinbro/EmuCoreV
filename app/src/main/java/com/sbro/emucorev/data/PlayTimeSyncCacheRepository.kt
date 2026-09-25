package com.sbro.emucorev.data

import android.content.Context
import androidx.core.content.edit
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject

class PlayTimeSyncCacheRepository(context: Context) {

    private val preferences = context.applicationContext.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
    private val mutex = Mutex()

    suspend fun add(entry: PlayerPlayTimeDelta) {
        if (entry.titleId.isBlank() || entry.durationMs <= 0L) return
        mutex.withLock {
            withContext(Dispatchers.IO) {
                val entries = readEntriesLocked().toMutableMap()
                val key = buildCacheKey(entry)
                val existing = entries[key]
                entries[key] = existing?.copy(
                    title = entry.title.ifBlank { existing.title },
                    coverArtPath = entry.coverArtPath ?: existing.coverArtPath,
                    durationMs = existing.durationMs + entry.durationMs,
                    sessionCount = existing.sessionCount + entry.sessionCount,
                    lastPlayedAtMs = maxOf(existing.lastPlayedAtMs, entry.lastPlayedAtMs)
                ) ?: entry
                writeEntriesLocked(entries.values.toList())
            }
        }
    }

    suspend fun drain(): List<PlayerPlayTimeDelta> {
        return mutex.withLock {
            withContext(Dispatchers.IO) {
                val entries = readEntriesLocked().values.toList()
                if (entries.isNotEmpty()) {
                    preferences.edit { remove(KEY_PENDING_ENTRIES) }
                }
                entries
            }
        }
    }

    suspend fun restore(entries: List<PlayerPlayTimeDelta>) {
        val validEntries = entries.filter { it.titleId.isNotBlank() && it.durationMs > 0L }
        if (validEntries.isEmpty()) return
        mutex.withLock {
            withContext(Dispatchers.IO) {
                val current = readEntriesLocked().toMutableMap()
                validEntries.forEach { entry ->
                    val key = buildCacheKey(entry)
                    val existing = current[key]
                    current[key] = existing?.copy(
                        title = entry.title.ifBlank { existing.title },
                        coverArtPath = entry.coverArtPath ?: existing.coverArtPath,
                        durationMs = existing.durationMs + entry.durationMs,
                        sessionCount = existing.sessionCount + entry.sessionCount,
                        lastPlayedAtMs = maxOf(existing.lastPlayedAtMs, entry.lastPlayedAtMs)
                    ) ?: entry
                }
                writeEntriesLocked(current.values.toList())
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

    private fun readEntriesLocked(): Map<String, PlayerPlayTimeDelta> {
        val raw = preferences.getString(KEY_PENDING_ENTRIES, null).orEmpty()
        if (raw.isBlank()) return emptyMap()
        return runCatching {
            val array = JSONArray(raw)
            buildMap {
                repeat(array.length()) { index ->
                    val json = array.optJSONObject(index) ?: return@repeat
                    val entry = PlayerPlayTimeDelta(
                        titleId = json.optString("titleId"),
                        title = json.optString("title"),
                        coverArtPath = json.optString("cover").takeIf { it.isNotBlank() },
                        durationMs = json.optLong("durationMs"),
                        sessionCount = json.optLong("sessions"),
                        lastPlayedAtMs = json.optLong("lastPlayedAtMs")
                    )
                    if (entry.titleId.isNotBlank() && entry.durationMs > 0L) {
                        put(buildCacheKey(entry), entry)
                    }
                }
            }
        }.getOrDefault(emptyMap())
    }

    private fun writeEntriesLocked(entries: List<PlayerPlayTimeDelta>) {
        val array = JSONArray()
        entries.filter { it.titleId.isNotBlank() && it.durationMs > 0L }.forEach { entry ->
            array.put(
                JSONObject()
                    .put("titleId", entry.titleId)
                    .put("title", entry.title)
                    .put("cover", entry.coverArtPath.orEmpty())
                    .put("durationMs", entry.durationMs)
                    .put("sessions", entry.sessionCount)
                    .put("lastPlayedAtMs", entry.lastPlayedAtMs)
            )
        }
        preferences.edit { putString(KEY_PENDING_ENTRIES, array.toString()) }
    }

    private fun buildCacheKey(entry: PlayerPlayTimeDelta): String = entry.titleId.uppercase()

    private companion object {
        const val PREFERENCES_NAME = "play_time_sync_cache"
        const val KEY_PENDING_ENTRIES = "pending_entries"
        const val KEY_LAST_CLOUD_SYNC_AT_MS = "last_cloud_sync_at_ms"
    }
}
