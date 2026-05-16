package com.sbro.emucorev.core

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.UUID
import kotlin.math.max

data class PlayTimeSession(
    val id: String,
    val titleId: String,
    val title: String,
    val startedAt: Long,
    val endedAt: Long?,
    val durationMs: Long
) {
    fun effectiveDurationMs(now: Long = System.currentTimeMillis()): Long {
        return if (endedAt == null) {
            max(0L, now - startedAt)
        } else {
            durationMs
        }
    }
}

class PlayTimeRepository(private val context: Context) {
    private val playTimeFile: File
        get() {
            val base = context.getExternalFilesDir(null) ?: context.filesDir
            return File(base, "play_time.json")
        }

    @Synchronized
    fun loadSessions(): List<PlayTimeSession> = readSessionsLocked()

    @Synchronized
    fun startSession(titleId: String, title: String, startedAt: Long = System.currentTimeMillis()): PlayTimeSession? {
        val normalizedTitleId = titleId.trim()
        if (normalizedTitleId.isBlank()) return null
        val sessions = readSessionsLocked().toMutableList()
        val closedSessions = sessions.map { session ->
            if (session.endedAt == null) {
                session.copy(
                    endedAt = startedAt,
                    durationMs = max(0L, startedAt - session.startedAt)
                )
            } else {
                session
            }
        }.toMutableList()
        val session = PlayTimeSession(
            id = "${normalizedTitleId}_${startedAt}_${UUID.randomUUID()}",
            titleId = normalizedTitleId,
            title = title.ifBlank { normalizedTitleId },
            startedAt = startedAt,
            endedAt = null,
            durationMs = 0L
        )
        closedSessions += session
        writeSessionsLocked(closedSessions)
        return session
    }

    @Synchronized
    fun finishSession(sessionId: String?, endedAt: Long = System.currentTimeMillis()) {
        if (sessionId.isNullOrBlank()) return
        val sessions = readSessionsLocked()
        val updated = sessions.map { session ->
            if (session.id == sessionId && session.endedAt == null) {
                session.copy(
                    endedAt = endedAt,
                    durationMs = max(0L, endedAt - session.startedAt)
                )
            } else {
                session
            }
        }
        writeSessionsLocked(updated)
    }

    @Synchronized
    fun finishOpenSessions(endedAt: Long = System.currentTimeMillis()) {
        val sessions = readSessionsLocked()
        val updated = sessions.map { session ->
            if (session.endedAt == null) {
                session.copy(
                    endedAt = endedAt,
                    durationMs = max(0L, endedAt - session.startedAt)
                )
            } else {
                session
            }
        }
        writeSessionsLocked(updated)
    }

    private fun readSessionsLocked(): List<PlayTimeSession> {
        if (!playTimeFile.exists()) return emptyList()
        return runCatching {
            val root = JSONObject(playTimeFile.readText())
            val sessions = root.optJSONArray("sessions") ?: JSONArray()
            buildList {
                for (index in 0 until sessions.length()) {
                    val item = sessions.optJSONObject(index) ?: continue
                    val titleId = item.optString("titleId").trim()
                    val startedAt = item.optLong("startedAt", 0L)
                    if (titleId.isBlank() || startedAt <= 0L) continue
                    val endedAt = item.takeIf { it.has("endedAt") && !it.isNull("endedAt") }?.optLong("endedAt")
                    add(
                        PlayTimeSession(
                            id = item.optString("id").ifBlank { "${titleId}_${startedAt}_legacy" },
                            titleId = titleId,
                            title = item.optString("title").ifBlank { titleId },
                            startedAt = startedAt,
                            endedAt = endedAt,
                            durationMs = max(0L, item.optLong("durationMs", endedAt?.minus(startedAt) ?: 0L))
                        )
                    )
                }
            }
        }.getOrDefault(emptyList())
    }

    private fun writeSessionsLocked(sessions: List<PlayTimeSession>) {
        playTimeFile.parentFile?.mkdirs()
        val root = JSONObject()
        val array = JSONArray()
        sessions.sortedBy { it.startedAt }.forEach { session ->
            array.put(
                JSONObject()
                    .put("id", session.id)
                    .put("titleId", session.titleId)
                    .put("title", session.title)
                    .put("startedAt", session.startedAt)
                    .put("durationMs", session.durationMs)
                    .put("endedAt", session.endedAt)
            )
        }
        root.put("sessions", array)
        playTimeFile.writeText(root.toString())
    }
}
