package com.sbro.emucorev.core

import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import androidx.core.content.FileProvider
import com.sbro.emucorev.BuildConfig
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
    val apkSizeBytes: Long?
) {
    val displayName: String = name.ifBlank { tagName }
    val hasInstallableApk: Boolean = !apkDownloadUrl.isNullOrBlank()
}

class AppUpdateRepository(private val context: Context) {

    fun checkLatestRelease(): AppUpdateRelease? {
        if (!hasNetwork()) return null
        val connection = openConnection(LATEST_RELEASE_URL, "application/vnd.github+json,application/json,*/*")
        return try {
            ensureSuccess(connection, "latest release")
            val json = connection.inputStream.bufferedReader().use { it.readText() }
            parseRelease(json).takeIf { isNewerThanCurrent(it.tagName) }
        } finally {
            connection.disconnect()
        }
    }

    fun downloadApk(release: AppUpdateRelease, onProgress: (Float) -> Unit): File {
        val downloadUrl = release.apkDownloadUrl ?: throw IOException("Release does not include an APK asset")
        val target = File(context.getExternalFilesDir("updates"), release.safeApkName())
        target.parentFile?.mkdirs()
        if (target.exists()) {
            target.delete()
        }

        val connection = openConnection(downloadUrl, "application/vnd.android.package-archive,application/octet-stream,*/*").apply {
            readTimeout = 90_000
        }
        try {
            ensureSuccess(connection, "update APK")
            val total = connection.contentLengthLong.takeIf { it > 0L } ?: release.apkSizeBytes ?: -1L
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
        val assets = root.optJSONArray("assets")
        var apkAssetName: String? = null
        var apkDownloadUrl: String? = null
        var apkSizeBytes: Long? = null
        if (assets != null) {
            for (index in 0 until assets.length()) {
                val asset = assets.getJSONObject(index)
                val name = asset.optString("name")
                if (!name.endsWith(".apk", ignoreCase = true)) continue
                apkAssetName = name
                apkDownloadUrl = asset.optString("browser_download_url").takeIf { it.isNotBlank() }
                apkSizeBytes = asset.optLong("size").takeIf { it > 0L }
                break
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
            apkSizeBytes = apkSizeBytes
        )
    }

    private fun AppUpdateRelease.safeApkName(): String {
        val rawName = apkAssetName?.ifBlank { null } ?: "EmuCoreV-${tagName.ifBlank { "update" }}.apk"
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

    private fun ensureSuccess(connection: HttpURLConnection, label: String) {
        val responseCode = connection.responseCode
        if (responseCode !in 200..299) {
            val responseMessage = connection.responseMessage.orEmpty().ifBlank { "HTTP $responseCode" }
            throw IOException("Could not load $label: $responseMessage")
        }
    }

    companion object {
        private const val LATEST_RELEASE_URL = "https://api.github.com/repos/sashkinbro/EmuCoreV/releases/latest"
    }
}
