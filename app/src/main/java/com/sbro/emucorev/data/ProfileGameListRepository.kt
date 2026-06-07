package com.sbro.emucorev.data

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

class ProfileGameListRepository(private val context: Context) {
    private val catalogRepository = VitaCatalogRepository(context)

    private val profileFile: File
        get() = File(context.filesDir, "profile_game_lists.json")

    fun loadEntries(): List<ProfileGameListEntry> {
        if (!profileFile.exists()) return emptyList()
        return runCatching {
            val root = JSONObject(profileFile.readText(Charsets.UTF_8))
            val items = root.optJSONArray("items") ?: JSONArray()
            buildList {
                for (index in 0 until items.length()) {
                    val item = items.optJSONObject(index) ?: continue
                    val igdbId = item.optLong("igdbId", 0L).takeIf { it > 0L } ?: continue
                    val status = item.optString("status").toProfileStatusOrNull()
                    val isFavorite = item.optBoolean("favorite", false)
                    if (status == null && !isFavorite) continue
                    add(
                        ProfileGameListEntry(
                            igdbId = igdbId,
                            status = status,
                            isFavorite = isFavorite,
                            addedAtEpochMillis = item.optLong("addedAt", 0L).takeIf { it > 0L } ?: item.optLong("updatedAt", 0L),
                            updatedAtEpochMillis = item.optLong("updatedAt", 0L)
                        )
                    )
                }
            }
        }.getOrDefault(emptyList())
            .distinctBy { it.igdbId }
            .sortedByDescending { it.updatedAtEpochMillis }
    }

    fun loadCatalogGames(): List<ProfileCatalogGame> {
        val entries = loadEntries()
        val catalogById = catalogRepository.getEntries(entries.map { it.igdbId }).associateBy { it.igdbId }
        return entries.mapNotNull { entry ->
            val catalog = catalogById[entry.igdbId] ?: return@mapNotNull null
            ProfileCatalogGame(profile = entry, catalog = catalog)
        }
    }

    fun setStatus(igdbId: Long, status: ProfileGameStatus) {
        if (igdbId <= 0L) return
        val now = System.currentTimeMillis()
        val current = loadEntries().toMutableList()
        val existingIndex = current.indexOfFirst { it.igdbId == igdbId }
        if (existingIndex >= 0) {
            val existing = current[existingIndex]
            current[existingIndex] = existing.copy(status = status, updatedAtEpochMillis = now)
        } else {
            current += ProfileGameListEntry(
                igdbId = igdbId,
                status = status,
                isFavorite = false,
                addedAtEpochMillis = now,
                updatedAtEpochMillis = now
            )
        }
        saveEntries(current)
    }

    fun clearStatus(igdbId: Long) {
        if (igdbId <= 0L) return
        val now = System.currentTimeMillis()
        val updated = loadEntries().mapNotNull { entry ->
            if (entry.igdbId != igdbId) return@mapNotNull entry
            entry.copy(status = null, updatedAtEpochMillis = now).takeIf { it.isFavorite }
        }
        saveEntries(updated)
    }

    fun setFavorite(igdbId: Long, isFavorite: Boolean) {
        if (igdbId <= 0L) return
        val now = System.currentTimeMillis()
        val current = loadEntries().toMutableList()
        val existingIndex = current.indexOfFirst { it.igdbId == igdbId }
        if (existingIndex >= 0) {
            val existing = current[existingIndex]
            val updated = existing.copy(isFavorite = isFavorite, updatedAtEpochMillis = now)
            if (updated.status == null && !updated.isFavorite) {
                current.removeAt(existingIndex)
            } else {
                current[existingIndex] = updated
            }
        } else if (isFavorite) {
            current += ProfileGameListEntry(
                igdbId = igdbId,
                status = null,
                isFavorite = true,
                addedAtEpochMillis = now,
                updatedAtEpochMillis = now
            )
        }
        saveEntries(current)
    }

    fun remove(igdbId: Long) {
        saveEntries(loadEntries().filterNot { it.igdbId == igdbId })
    }

    fun replaceAll(entries: List<ProfileGameListEntry>) {
        saveEntries(entries)
    }

    private fun saveEntries(entries: List<ProfileGameListEntry>) {
        profileFile.parentFile?.mkdirs()
        val normalized = entries
            .filter { it.igdbId > 0L }
            .distinctBy { it.igdbId }
            .sortedByDescending { it.updatedAtEpochMillis }
        val root = JSONObject()
            .put("version", 1)
            .put(
                "items",
                JSONArray().apply {
                    normalized.forEach { entry ->
                        put(
                            JSONObject()
                                .put("igdbId", entry.igdbId)
                                .put("status", entry.status?.name ?: "")
                                .put("favorite", entry.isFavorite)
                                .put("addedAt", entry.addedAtEpochMillis)
                                .put("updatedAt", entry.updatedAtEpochMillis)
                        )
                    }
                }
            )
        profileFile.writeText(root.toString(2), Charsets.UTF_8)
    }

    private fun String.toProfileStatusOrNull(): ProfileGameStatus? {
        return ProfileGameStatus.entries.firstOrNull { it.name == this }
    }
}
