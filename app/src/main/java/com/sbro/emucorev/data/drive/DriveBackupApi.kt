package com.sbro.emucorev.data.drive

import android.content.Context
import android.util.Log
import com.google.android.gms.auth.api.identity.ClearTokenRequest
import com.google.android.gms.auth.api.identity.Identity
import com.sbro.emucorev.core.BackupSessionGate
import com.sbro.emucorev.data.drive.DriveBackupArchive.Companion.hex
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.CancellationException
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.net.HttpURLConnection
import java.net.URI
import java.net.URL
import java.net.URLEncoder
import java.security.MessageDigest

data class DriveBackupEntry(
    val id: String, val name: String, val device: String, val deviceId: String,
    val createdAt: Long, val size: Long, val coreVersion: String, val md5: String
)

/** Small REST client with bounded buffers; no Drive-wide permission and no token persisted to disk. */
class DriveBackupApi(private val context: Context, private val email: String) {
    private var token = ""
    private var tokenAt = 0L

    private suspend fun connection(url: String, method: String): HttpURLConnection {
        currentCoroutineContext().ensureActive()
        val uri = URI(url)
        require(uri.scheme == "https" && uri.host == "www.googleapis.com" && uri.port == -1)
        if (token.isEmpty() || System.currentTimeMillis() - tokenAt > 45 * 60_000) {
            token = DriveAuthorization.token(context, email); tokenAt = System.currentTimeMillis()
        }
        return (URL(url).openConnection() as HttpURLConnection).apply {
            requestMethod = method
            instanceFollowRedirects = false
            connectTimeout = 20_000; readTimeout = 30_000
            setRequestProperty("Authorization", "Bearer $token")
        }
    }

    private suspend fun failure(code: Int, body: String): Nothing {
        val googleError = runCatching { JSONObject(body).optJSONObject("error") }.getOrNull()
        val details = googleError?.optJSONArray("details")
        val reasons = buildList {
            googleError?.optJSONArray("errors")?.let { errors ->
                for (i in 0 until errors.length()) add(errors.optJSONObject(i)?.optString("reason").orEmpty())
            }
            if (details != null) for (i in 0 until details.length()) add(details.optJSONObject(i)?.optString("reason").orEmpty())
        }.filter { it.matches(Regex("[A-Za-z_]+")) }
        // Never log tokens, account addresses, request URLs or the response body.
        Log.w("DriveBackup", "Drive API failed: HTTP $code, reasons=${reasons.joinToString()}")
        if (code == 401 && token.isNotEmpty()) {
            try { Identity.getAuthorizationClient(context).clearToken(ClearTokenRequest.builder().setToken(token).build()).awaitDrive() }
            catch (error: CancellationException) { throw error }
            catch (_: Exception) { }
        }
        val reason = when {
            code == 401 -> { token = ""; "auth" }
            code == 404 -> "missing"
            body.contains("storageQuotaExceeded") -> "quota"
            body.contains("accessNotConfigured") || body.contains("SERVICE_DISABLED") -> "configuration"
            code == 429 || code >= 500 || body.contains("rateLimitExceeded", true) -> "network"
            code == 403 -> "permission"
            else -> "network"
        }
        throw DriveBackupException(reason)
    }

    private suspend fun json(path: String, method: String = "GET", body: JSONObject? = null): JSONObject {
        val conn = connection("$API/$path", method)
        try {
            if (body != null) {
                conn.doOutput = true; conn.setRequestProperty("Content-Type", "application/json; charset=UTF-8")
                conn.outputStream.use { it.write(body.toString().toByteArray()) }
            }
            val code = conn.responseCode
            if (code !in 200..299) failure(code, conn.errorStream?.bufferedReader()?.use { it.readText().take(8192) }.orEmpty())
            if (code == 204) return JSONObject()
            return JSONObject(conn.inputStream.bufferedReader().use { it.readText() })
        } finally { conn.disconnect() }
    }

    suspend fun verifyAccount(): DriveAccountProfile {
        val user = json("about?fields=user(emailAddress,displayName,photoLink)").getJSONObject("user")
        if (!user.getString("emailAddress").equals(email, true)) throw DriveBackupException("auth")
        return DriveAccountProfile(user.optString("displayName"),
            user.optString("photoLink").takeIf { it.startsWith("https://") }.orEmpty())
    }

    suspend fun list(): List<DriveBackupEntry> {
        val result = mutableListOf<DriveBackupEntry>()
        var next = ""
        do {
            val query = "trashed = false and appProperties has { key='emucorevBackup' and value='1' } and appProperties has { key='complete' and value='1' }"
            val response = json("files?q=${enc(query)}&spaces=drive&pageSize=100&fields=nextPageToken,files(id,name,size,md5Checksum,appProperties)&pageToken=${enc(next)}")
            val files = response.optJSONArray("files") ?: JSONArray()
            for (i in 0 until files.length()) {
                val file = files.getJSONObject(i)
                val props = file.getJSONObject("appProperties")
                result += DriveBackupEntry(file.getString("id"), file.optString("name"), props.optString("device"),
                    props.optString("deviceId"), props.optString("createdAt").toLongOrNull() ?: 0,
                    file.optString("size").toLongOrNull() ?: 0, props.optString("coreVersion"), file.optString("md5Checksum"))
            }
            next = response.optString("nextPageToken")
        } while (next.isNotBlank())
        return result.sortedByDescending { it.createdAt }
    }

    suspend fun trash(id: String) { json("files/${enc(id)}", "PATCH", JSONObject().put("trashed", true)) }

    private suspend fun folder(): String {
        val q = "trashed = false and mimeType = 'application/vnd.google-apps.folder' and appProperties has { key='emucorevFolder' and value='1' }"
        val files = json("files?q=${enc(q)}&fields=files(id)&pageSize=100").getJSONArray("files")
        if (files.length() > 0) return files.getJSONObject(0).getString("id")
        return json("files?fields=id", "POST", JSONObject().put("name", "EmuCoreV Backups")
            .put("mimeType", "application/vnd.google-apps.folder")
            .put("appProperties", JSONObject().put("emucorevFolder", "1"))).getString("id")
    }

    /** The immutable snapshot and resumable URL survive WorkManager retries and process death. */
    suspend fun upload(snapshot: DriveSnapshot, verifying: suspend () -> Unit = {}, progress: suspend (Long, Long) -> Unit): String {
        val sessionFile = File(snapshot.file.parentFile, "upload.json")
        val previous = runCatching { JSONObject(sessionFile.readText()) }.getOrNull()
        val session = previous?.takeIf { it.optString("digest") == snapshot.digest && it.optString("email") == email }
            ?: JSONObject().put("digest", snapshot.digest).put("email", email)
        var id = session.optString("id")
        if (id.isBlank()) {
            // Pre-generated IDs make retries idempotent, even if a response is lost.
            id = json("files/generateIds?count=1&space=drive&type=files").getJSONArray("ids").getString(0)
            session.put("id", id)
            DriveBackupArchive.writeAtomic(sessionFile, session)
        }
        val props = JSONObject().put("emucorevBackup", "1").put("complete", "0")
            .put("device", snapshot.manifest.optString("device").take(100))
            .put("deviceId", snapshot.manifest.getString("deviceId"))
            .put("createdAt", snapshot.manifest.getLong("createdAt").toString())
            .put("coreVersion", snapshot.manifest.getString("coreVersion"))
        var url = session.optString("url")
        var offset = 0L
        if (url.isNotBlank()) {
            val conn = connection(url, "PUT")
            try {
                conn.doOutput = true; conn.setFixedLengthStreamingMode(0)
                conn.setRequestProperty("Content-Range", "bytes */${snapshot.file.length()}")
                conn.outputStream.close()
                when (val code = conn.responseCode) {
                    200, 201 -> offset = snapshot.file.length()
                    308 -> offset = conn.getHeaderField("Range")?.substringAfterLast('-')?.toLongOrNull()?.plus(1) ?: 0
                    404, 410 -> url = ""
                    else -> failure(code, conn.errorStream?.bufferedReader()?.use { it.readText() }.orEmpty())
                }
            } finally { conn.disconnect() }
        }
        if (url.isBlank()) {
            val metadata = JSONObject().put("id", id).put("name", "EmuCoreV-${snapshot.manifest.getLong("createdAt")}.zip.uploading")
                .put("parents", JSONArray().put(folder())).put("mimeType", "application/zip").put("appProperties", props)
            // If a previous session completed just before a crash, verify the existing draft instead.
            val existing = try { json("files/${enc(id)}?fields=id,size,md5Checksum,appProperties") }
                catch (e: DriveBackupException) { if (e.reason != "missing") throw e else null }
            if (existing?.optString("size")?.toLongOrNull() == snapshot.file.length() && existing.optString("md5Checksum").isNotEmpty()) offset = snapshot.file.length()
            else {
                if (existing != null) { metadata.remove("id"); metadata.remove("parents") }
                val path = if (existing == null) "files" else "files/${enc(id)}"
                val conn = connection("https://www.googleapis.com/upload/drive/v3/$path?uploadType=resumable&fields=id", if (existing == null) "POST" else "PATCH")
                try {
                    conn.doOutput = true
                    conn.setRequestProperty("Content-Type", "application/json; charset=UTF-8")
                    conn.setRequestProperty("X-Upload-Content-Type", "application/zip")
                    conn.setRequestProperty("X-Upload-Content-Length", snapshot.file.length().toString())
                    conn.outputStream.use { it.write(metadata.toString().toByteArray()) }
                    val code = conn.responseCode
                    if (code !in 200..299) failure(code, conn.errorStream?.bufferedReader()?.use { it.readText() }.orEmpty())
                    url = conn.getHeaderField("Location") ?: throw IOException("Missing upload location")
                    session.put("url", url); DriveBackupArchive.writeAtomic(sessionFile, session)
                } finally { conn.disconnect() }
            }
        }
        progress(offset, snapshot.file.length())
        RandomAccessFile(snapshot.file, "r").use { input ->
            val buffer = ByteArray(1024 * 1024)
            while (offset < input.length()) {
                currentCoroutineContext().ensureActive(); BackupSessionGate.checkpoint()
                input.seek(offset)
                val count = input.read(buffer)
                if (count <= 0) throw IOException("Snapshot truncated")
                val conn = connection(url, "PUT")
                try {
                    conn.doOutput = true; conn.setFixedLengthStreamingMode(count)
                    conn.setRequestProperty("Content-Type", "application/zip")
                    conn.setRequestProperty("Content-Range", "bytes $offset-${offset + count - 1}/${input.length()}")
                    conn.outputStream.use { output ->
                        var sent = 0
                        while (sent < count) {
                            currentCoroutineContext().ensureActive(); BackupSessionGate.checkpoint()
                            val bytes = minOf(64 * 1024, count - sent)
                            output.write(buffer, sent, bytes)
                            sent += bytes
                            // Keep 100% for the server's acknowledgement of the final chunk.
                            progress(minOf(offset + sent, input.length() - 1), input.length())
                        }
                    }
                    when (val code = conn.responseCode) {
                        200, 201 -> offset = input.length()
                        308 -> {
                            val next = conn.getHeaderField("Range")?.substringAfterLast('-')?.toLongOrNull()?.plus(1) ?: 0
                            if (next <= offset || next > offset + count) throw IOException("Invalid upload offset")
                            offset = next
                        }
                        else -> failure(code, conn.errorStream?.bufferedReader()?.use { it.readText() }.orEmpty())
                    }
                    progress(offset, input.length())
                } finally { conn.disconnect() }
            }
        }
        verifying()
        val remote = json("files/${enc(id)}?fields=size,md5Checksum")
        val md5 = MessageDigest.getInstance("MD5")
        snapshot.file.inputStream().use { input ->
            val buffer = ByteArray(128 * 1024)
            while (true) { currentCoroutineContext().ensureActive(); BackupSessionGate.checkpoint(); val n = input.read(buffer); if (n < 0) break; md5.update(buffer, 0, n) }
        }
        if (remote.optString("size").toLongOrNull() != snapshot.file.length() || remote.optString("md5Checksum") != md5.digest().hex()) throw DriveBackupException("invalid")
        json("files/${enc(id)}", "PATCH", JSONObject().put("appProperties", props.put("complete", "1"))
            .put("name", "EmuCoreV-${snapshot.manifest.getLong("createdAt")}.zip"))
        sessionFile.delete()
        return id
    }

    suspend fun download(entry: DriveBackupEntry, target: File, progress: suspend (Long, Long) -> Unit) {
        if (entry.size <= 0 || entry.size > target.parentFile!!.usableSpace - 32 * 1024 * 1024) throw DriveBackupException("space")
        val conn = connection("$API/files/${enc(entry.id)}?alt=media", "GET")
        try {
            val code = conn.responseCode
            if (code != 200) failure(code, conn.errorStream?.bufferedReader()?.use { it.readText() }.orEmpty())
            val hash = MessageDigest.getInstance("MD5")
            var total = 0L
            progress(0, entry.size)
            conn.inputStream.use { input -> target.outputStream().use { output ->
                val buffer = ByteArray(128 * 1024)
                while (true) {
                    currentCoroutineContext().ensureActive(); BackupSessionGate.checkpoint()
                    val n = input.read(buffer); if (n < 0) break
                    total += n
                    if (total > entry.size) throw DriveBackupException("invalid")
                    hash.update(buffer, 0, n); output.write(buffer, 0, n); progress(total, entry.size)
                }
            } }
            if (total != entry.size || hash.digest().hex() != entry.md5) throw DriveBackupException("invalid")
        } finally { conn.disconnect() }
    }

    companion object {
        private const val API = "https://www.googleapis.com/drive/v3"
        private fun enc(value: String) = URLEncoder.encode(value, "UTF-8")
    }
}

data class DriveAccountProfile(val displayName: String, val photoUrl: String)
