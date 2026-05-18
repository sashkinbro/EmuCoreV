package com.sbro.emucorev.data

import android.content.Context
import androidx.core.content.edit
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

data class AchievementAssistResult(
    val title: String,
    val body: String
)

class AchievementAssistRepository(context: Context) {
    private val prefs = context.applicationContext.getSharedPreferences("achievement_assist_cache", Context.MODE_PRIVATE)

    suspend fun translate(
        languageTag: String,
        languageName: String,
        gameTitle: String,
        trophyName: String,
        trophyDetail: String,
        trophyGrade: String
    ): AchievementAssistResult = request(
        action = "translate",
        languageTag = languageTag,
        languageName = languageName,
        gameTitle = gameTitle,
        trophyName = trophyName,
        trophyDetail = trophyDetail,
        trophyGrade = trophyGrade
    )

    suspend fun hint(
        languageTag: String,
        languageName: String,
        gameTitle: String,
        trophyName: String,
        trophyDetail: String,
        trophyGrade: String
    ): AchievementAssistResult = request(
        action = "hint",
        languageTag = languageTag,
        languageName = languageName,
        gameTitle = gameTitle,
        trophyName = trophyName,
        trophyDetail = trophyDetail,
        trophyGrade = trophyGrade
    )

    private suspend fun request(
        action: String,
        languageTag: String,
        languageName: String,
        gameTitle: String,
        trophyName: String,
        trophyDetail: String,
        trophyGrade: String
    ): AchievementAssistResult = withContext(Dispatchers.IO) {
        val cacheKey = cacheKey(action, languageTag, gameTitle, trophyName, trophyDetail, trophyGrade)
        readCache(cacheKey)?.let { return@withContext it }

        val payload = JSONObject()
            .put("action", action)
            .put("targetLanguageTag", languageTag)
            .put("targetLanguageName", languageName)
            .put("gameTitle", gameTitle)
            .put("trophyName", trophyName)
            .put("trophyDetail", trophyDetail)
            .put("trophyGrade", trophyGrade)

        val connection = (URL(WORKER_URL).openConnection() as HttpURLConnection).apply {
            requestMethod = "POST"
            connectTimeout = 12_000
            readTimeout = 35_000
            doOutput = true
            setRequestProperty("Accept", "application/json")
            setRequestProperty("Content-Type", "application/json; charset=utf-8")
            setRequestProperty("X-EmuCoreV-Assist-Key", WORKER_ACCESS_KEY)
        }

        try {
            connection.outputStream.bufferedWriter(Charsets.UTF_8).use { writer ->
                writer.write(payload.toString())
            }
            val body = if (connection.responseCode in 200..299) {
                connection.inputStream.bufferedReader(Charsets.UTF_8).use { it.readText() }
            } else {
                val error = connection.errorStream?.bufferedReader(Charsets.UTF_8)?.use { it.readText() }.orEmpty()
                throw IllegalStateException("Worker failed: ${connection.responseCode} $error")
            }
            parseResult(body).also { writeCache(cacheKey, it) }
        } finally {
            connection.disconnect()
        }
    }

    private fun parseResult(body: String): AchievementAssistResult {
        val root = JSONObject(body)
        if (!root.optBoolean("ok", false)) {
            throw IllegalStateException(root.optString("error").ifBlank { "Worker returned an error" })
        }
        val result = root.optJSONObject("result") ?: root
        val title = result.optString("title").trim()
        val text = result.optString("body").trim()
        if (title.isBlank() && text.isBlank()) {
            throw IllegalStateException("Worker returned an empty result")
        }
        return AchievementAssistResult(
            title = title,
            body = text
        )
    }

    private fun readCache(key: String): AchievementAssistResult? {
        val raw = prefs.getString(key, null) ?: return null
        return runCatching {
            val root = JSONObject(raw)
            if (System.currentTimeMillis() - root.optLong("createdAt") > LOCAL_CACHE_TTL_MS) return null
            AchievementAssistResult(
                title = root.optString("title"),
                body = root.optString("body")
            ).takeIf { it.title.isNotBlank() || it.body.isNotBlank() }
        }.getOrNull()
    }

    private fun writeCache(key: String, result: AchievementAssistResult) {
        val raw = JSONObject()
            .put("createdAt", System.currentTimeMillis())
            .put("title", result.title)
            .put("body", result.body)
            .toString()
        prefs.edit { putString(key, raw) }
    }

    private fun cacheKey(
        action: String,
        languageTag: String,
        gameTitle: String,
        trophyName: String,
        trophyDetail: String,
        trophyGrade: String
    ): String {
        val input = listOf(action, languageTag, gameTitle, trophyName, trophyDetail, trophyGrade)
            .joinToString(separator = "\u001F")
            .lowercase()
        val digest = MessageDigest.getInstance("SHA-256").digest(input.toByteArray(Charsets.UTF_8))
        return digest.joinToString(prefix = "assist_", separator = "") { "%02x".format(it) }
    }

    private companion object {
        const val WORKER_URL = "https://emucorevachievementshelp.bobtyrenso.workers.dev"
        const val WORKER_ACCESS_KEY = "emucorev-achievements-help-2026"
        const val LOCAL_CACHE_TTL_MS = 7L * 24L * 60L * 60L * 1000L
    }
}
