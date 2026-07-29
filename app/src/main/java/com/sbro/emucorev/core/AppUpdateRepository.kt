package com.sbro.emucorev.core

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import androidx.core.content.edit
import com.sbro.emucorev.BuildConfig
import org.json.JSONArray
import org.json.JSONObject
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

data class AppUpdateRelease(
    val tagName: String,
    val name: String,
    val body: String,
    val publishedAt: String,
    val htmlUrl: String,
    val apkAssetName: String?,
    val apkDownloadUrl: String?,
    val apkSizeBytes: Long?,
    val parallelApkAssetName: String? = null,
    val parallelApkDownloadUrl: String? = null,
    val parallelApkSizeBytes: Long? = null
) {
    val displayName: String = name.ifBlank { tagName }
    val hasInstallableApk: Boolean = !apkDownloadUrl.isNullOrBlank()
    val hasParallelApk: Boolean = !parallelApkDownloadUrl.isNullOrBlank()
}

class AppUpdateRepository(private val context: Context) {
    private val cachePrefs = context.applicationContext.getSharedPreferences(CACHE_PREFS_NAME, Context.MODE_PRIVATE)

    fun loadReleaseHistory(forceRefresh: Boolean = false): List<AppUpdateRelease> {
        val json = loadCachedJson(
            keyPrefix = CACHE_KEY_HISTORY,
            url = RELEASES_URL,
            accept = GITHUB_JSON_ACCEPT,
            label = "release history",
            forceRefresh = forceRefresh
        ) ?: return emptyList()
        return runCatching {
            parseReleaseList(json)
        }.getOrDefault(emptyList())
    }

    private fun parseRelease(json: String): AppUpdateRelease {
        val root = JSONObject(json)
        return parseRelease(root)
    }

    private fun parseReleaseList(json: String): List<AppUpdateRelease> {
        val items = JSONArray(json)
        return buildList {
            for (index in 0 until items.length()) {
                val item = items.optJSONObject(index) ?: continue
                add(parseRelease(item))
            }
        }
    }

    private fun parseRelease(root: JSONObject): AppUpdateRelease {
        val assets = root.optJSONArray("assets")
        var apkAssetName: String? = null
        var apkDownloadUrl: String? = null
        var apkSizeBytes: Long? = null
        var parallelApkAssetName: String? = null
        var parallelApkDownloadUrl: String? = null
        var parallelApkSizeBytes: Long? = null
        if (assets != null) {
            for (index in 0 until assets.length()) {
                val asset = assets.getJSONObject(index)
                val name = asset.optString("name")
                if (!name.endsWith(".apk", ignoreCase = true)) continue
                val downloadUrl = asset.optString("browser_download_url").takeIf { it.isNotBlank() }
                val sizeBytes = asset.optLong("size").takeIf { it > 0L }
                if (name.contains("parallel", ignoreCase = true)) {
                    if (parallelApkDownloadUrl == null) {
                        parallelApkAssetName = name
                        parallelApkDownloadUrl = downloadUrl
                        parallelApkSizeBytes = sizeBytes
                    }
                } else if (apkDownloadUrl == null) {
                    apkAssetName = name
                    apkDownloadUrl = downloadUrl
                    apkSizeBytes = sizeBytes
                }
            }
        }
        return AppUpdateRelease(
            tagName = root.optString("tag_name"),
            name = root.optString("name"),
            body = root.optString("body"),
            publishedAt = root.optString("published_at"),
            htmlUrl = root.optString("html_url"),
            apkAssetName = apkAssetName,
            apkDownloadUrl = apkDownloadUrl,
            apkSizeBytes = apkSizeBytes,
            parallelApkAssetName = parallelApkAssetName,
            parallelApkDownloadUrl = parallelApkDownloadUrl,
            parallelApkSizeBytes = parallelApkSizeBytes
        )
    }

    private fun hasNetwork(): Boolean {
        val manager = context.getSystemService(ConnectivityManager::class.java) ?: return false
        val network = manager.activeNetwork ?: return false
        val capabilities = manager.getNetworkCapabilities(network) ?: return false
        return capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
    }

    private fun openConnection(url: String, accept: String): HttpURLConnection {
        return (URL(url).openConnection() as HttpURLConnection).apply {
            connectTimeout = 10_000
            readTimeout = 20_000
            instanceFollowRedirects = true
            setRequestProperty("Accept", accept)
            setRequestProperty("X-GitHub-Api-Version", "2022-11-28")
            setRequestProperty("User-Agent", "EmuCoreV/${BuildConfig.VERSION_NAME}")
        }
    }

    private fun openCachedConnection(
        url: String,
        accept: String,
        cachedEtag: String?,
        cachedLastModified: String?
    ): HttpURLConnection {
        return openConnection(url, accept).apply {
            cachedEtag?.takeIf(String::isNotBlank)?.let { setRequestProperty("If-None-Match", it) }
            cachedLastModified?.takeIf(String::isNotBlank)?.let { setRequestProperty("If-Modified-Since", it) }
        }
    }

    private fun loadCachedJson(
        keyPrefix: String,
        url: String,
        accept: String,
        label: String,
        forceRefresh: Boolean = false
    ): String? {
        val now = System.currentTimeMillis()
        val cachedJson = cachePrefs.getString(cacheKey(keyPrefix, "json"), null)
        val fetchedAt = cachePrefs.getLong(cacheKey(keyPrefix, "fetched_at"), 0L)
        val errorAt = cachePrefs.getLong(cacheKey(keyPrefix, "error_at"), 0L)
        val cachedError = cachePrefs.getString(cacheKey(keyPrefix, "error"), null)

        if (!forceRefresh && !cachedJson.isNullOrBlank() && now - fetchedAt < CACHE_TTL_MILLIS) {
            return cachedJson
        }
        if (!forceRefresh && cachedJson.isNullOrBlank() && !cachedError.isNullOrBlank() && now - errorAt < CACHE_TTL_MILLIS) {
            throw IOException(cachedError)
        }
        if (!hasNetwork()) {
            return cachedJson
        }

        val cachedEtag = cachePrefs.getString(cacheKey(keyPrefix, "etag"), null)
        val cachedLastModified = cachePrefs.getString(cacheKey(keyPrefix, "last_modified"), null)
        val connection = openCachedConnection(url, accept, cachedEtag, cachedLastModified)
        return try {
            when (val responseCode = connection.responseCode) {
                HttpURLConnection.HTTP_NOT_MODIFIED -> {
                    if (!cachedJson.isNullOrBlank()) {
                        markCacheFresh(keyPrefix, now)
                        cachedJson
                    } else {
                        null
                    }
                }

                in 200..299 -> {
                    val json = connection.inputStream.bufferedReader().use { it.readText() }
                    cacheJson(
                        keyPrefix = keyPrefix,
                        json = json,
                        etag = connection.getHeaderField("ETag"),
                        lastModified = connection.getHeaderField("Last-Modified"),
                        fetchedAt = now
                    )
                    json
                }

                else -> {
                    val responseMessage = connection.responseMessage.orEmpty().ifBlank { "HTTP $responseCode" }
                    throw IOException("Could not load $label: $responseMessage")
                }
            }
        } catch (error: IOException) {
            cacheError(keyPrefix, error.message ?: "Could not load $label", now)
            cachedJson ?: throw error
        } finally {
            connection.disconnect()
        }
    }

    private fun cacheJson(
        keyPrefix: String,
        json: String,
        etag: String?,
        lastModified: String?,
        fetchedAt: Long
    ) {
        cachePrefs.edit {
            putString(cacheKey(keyPrefix, "json"), json)
            putLong(cacheKey(keyPrefix, "fetched_at"), fetchedAt)
            putString(cacheKey(keyPrefix, "etag"), etag.orEmpty())
            putString(cacheKey(keyPrefix, "last_modified"), lastModified.orEmpty())
            remove(cacheKey(keyPrefix, "error"))
            remove(cacheKey(keyPrefix, "error_at"))
        }
    }

    private fun markCacheFresh(keyPrefix: String, fetchedAt: Long) {
        cachePrefs.edit {
            putLong(cacheKey(keyPrefix, "fetched_at"), fetchedAt)
            remove(cacheKey(keyPrefix, "error"))
            remove(cacheKey(keyPrefix, "error_at"))
        }
    }

    private fun cacheError(keyPrefix: String, message: String, errorAt: Long) {
        cachePrefs.edit {
            putString(cacheKey(keyPrefix, "error"), message)
            putLong(cacheKey(keyPrefix, "error_at"), errorAt)
        }
    }

    private fun cacheKey(prefix: String, suffix: String): String = "${prefix}_$suffix"

    private fun ensureSuccess(connection: HttpURLConnection, label: String) {
        val responseCode = connection.responseCode
        if (responseCode !in 200..299) {
            val responseMessage = connection.responseMessage.orEmpty().ifBlank { "HTTP $responseCode" }
            throw IOException("Could not load $label: $responseMessage")
        }
    }

    companion object {
        private const val CACHE_PREFS_NAME = "app_update_cache"
        private const val CACHE_KEY_HISTORY = "release_history"
        private const val CACHE_TTL_MILLIS = 15 * 60 * 1000L
        private const val GITHUB_JSON_ACCEPT = "application/vnd.github+json,application/json,*/*"
        private const val RELEASES_URL = "https://api.github.com/repos/sashkinbro/EmuCoreV/releases?per_page=100"
    }
}
