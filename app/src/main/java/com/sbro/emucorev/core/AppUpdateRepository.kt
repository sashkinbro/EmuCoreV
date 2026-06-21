package com.sbro.emucorev.core

import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import androidx.core.content.edit
import androidx.core.content.FileProvider
import com.sbro.emucorev.BuildConfig
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
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

    fun checkLatestRelease(): AppUpdateRelease? {
        val json = loadCachedJson(
            keyPrefix = CACHE_KEY_LATEST,
            url = LATEST_RELEASE_URL,
            accept = GITHUB_JSON_ACCEPT,
            label = "latest release"
        ) ?: return null
        return runCatching {
            parseRelease(json).takeIf { isNewerThanCurrent(it.tagName) }
        }.getOrNull()
    }

    fun loadReleaseHistory(): List<AppUpdateRelease> {
        val json = loadCachedJson(
            keyPrefix = CACHE_KEY_HISTORY,
            url = RELEASES_URL,
            accept = GITHUB_JSON_ACCEPT,
            label = "release history"
        ) ?: return emptyList()
        return runCatching {
            parseReleaseList(json)
        }.getOrDefault(emptyList())
    }

    fun downloadApk(release: AppUpdateRelease, onProgress: (Float) -> Unit): File {
        val downloadUrl = release.apkDownloadUrl ?: throw IOException("Release does not include an APK asset")
        val target = File(context.getExternalFilesDir("updates"), release.safeApkName())
        return downloadApkAsset(
            downloadUrl = downloadUrl,
            target = target,
            expectedSizeBytes = release.apkSizeBytes,
            label = "update APK",
            onProgress = onProgress
        )
    }

    fun downloadParallelApk(release: AppUpdateRelease, onProgress: (Float) -> Unit): File {
        val downloadUrl = release.parallelApkDownloadUrl ?: throw IOException("Release does not include a parallel APK asset")
        val target = File(context.getExternalFilesDir("updates"), release.safeParallelApkName())
        return downloadApkAsset(
            downloadUrl = downloadUrl,
            target = target,
            expectedSizeBytes = release.parallelApkSizeBytes,
            label = "parallel APK",
            onProgress = onProgress
        )
    }

    private fun downloadApkAsset(
        downloadUrl: String,
        target: File,
        expectedSizeBytes: Long?,
        label: String,
        onProgress: (Float) -> Unit
    ): File {
        target.parentFile?.mkdirs()
        if (target.exists()) {
            target.delete()
        }

        val connection = openConnection(downloadUrl, "application/vnd.android.package-archive,application/octet-stream,*/*").apply {
            readTimeout = 90_000
        }
        try {
            ensureSuccess(connection, label)
            val total = connection.contentLengthLong.takeIf { it > 0L } ?: expectedSizeBytes ?: -1L
            var copied = 0L
            connection.inputStream.use { input ->
                target.outputStream().use { output ->
                    val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                    while (true) {
                        val read = input.read(buffer)
                        if (read <= 0) break
                        output.write(buffer, 0, read)
                        copied += read
                        if (total > 0L) {
                            onProgress((copied.toFloat() / total.toFloat()).coerceIn(0f, 1f))
                        }
                    }
                }
            }
        } finally {
            connection.disconnect()
        }
        onProgress(1f)
        return target
    }

    fun launchInstaller(apkFile: File) {
        val apkUri = FileProvider.getUriForFile(context, "${context.packageName}.fileprovider", apkFile)
        val intent = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(apkUri, "application/vnd.android.package-archive")
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        context.startActivity(intent)
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

    private fun AppUpdateRelease.safeApkName(): String {
        val rawName = apkAssetName?.ifBlank { null } ?: "EmuCoreV-${tagName.ifBlank { "update" }}.apk"
        return rawName.replace(Regex("[^A-Za-z0-9._-]"), "_")
    }

    private fun AppUpdateRelease.safeParallelApkName(): String {
        val rawName = parallelApkAssetName?.ifBlank { null } ?: "EmuCoreV-${tagName.ifBlank { "parallel" }}-parallel.apk"
        return rawName.replace(Regex("[^A-Za-z0-9._-]"), "_")
    }

    private fun hasNetwork(): Boolean {
        val manager = context.getSystemService(ConnectivityManager::class.java) ?: return false
        val network = manager.activeNetwork ?: return false
        val capabilities = manager.getNetworkCapabilities(network) ?: return false
        return capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
    }

    private fun isNewerThanCurrent(remoteTag: String): Boolean {
        val remote = parseVersion(remoteTag)
        val current = parseVersion(BuildConfig.VERSION_NAME)
        return if (remote != null && current != null) {
            compareVersions(remote, current) > 0
        } else {
            remoteTag.trim().removePrefix("v").isNotBlank() &&
                !remoteTag.trim().removePrefix("v").equals(BuildConfig.VERSION_NAME.trim().removePrefix("v"), ignoreCase = true)
        }
    }

    private fun parseVersion(value: String): List<Int>? {
        val parts = value.trim()
            .removePrefix("v")
            .substringBefore('-')
            .split('.')
            .mapNotNull { part -> part.takeWhile(Char::isDigit).toIntOrNull() }
        return parts.takeIf { it.isNotEmpty() }
    }

    private fun compareVersions(left: List<Int>, right: List<Int>): Int {
        val maxSize = maxOf(left.size, right.size)
        for (index in 0 until maxSize) {
            val l = left.getOrNull(index) ?: 0
            val r = right.getOrNull(index) ?: 0
            if (l != r) return l.compareTo(r)
        }
        return 0
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
        label: String
    ): String? {
        val now = System.currentTimeMillis()
        val cachedJson = cachePrefs.getString(cacheKey(keyPrefix, "json"), null)
        val fetchedAt = cachePrefs.getLong(cacheKey(keyPrefix, "fetched_at"), 0L)
        val errorAt = cachePrefs.getLong(cacheKey(keyPrefix, "error_at"), 0L)
        val cachedError = cachePrefs.getString(cacheKey(keyPrefix, "error"), null)

        if (!cachedJson.isNullOrBlank() && now - fetchedAt < CACHE_TTL_MILLIS) {
            return cachedJson
        }
        if (cachedJson.isNullOrBlank() && !cachedError.isNullOrBlank() && now - errorAt < CACHE_TTL_MILLIS) {
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
        private const val CACHE_KEY_LATEST = "latest_release"
        private const val CACHE_KEY_HISTORY = "release_history"
        private const val CACHE_TTL_MILLIS = 15 * 60 * 1000L
        private const val GITHUB_JSON_ACCEPT = "application/vnd.github+json,application/json,*/*"
        private const val LATEST_RELEASE_URL = "https://api.github.com/repos/sashkinbro/EmuCoreV/releases/latest"
        private const val RELEASES_URL = "https://api.github.com/repos/sashkinbro/EmuCoreV/releases?per_page=100"
    }
}
