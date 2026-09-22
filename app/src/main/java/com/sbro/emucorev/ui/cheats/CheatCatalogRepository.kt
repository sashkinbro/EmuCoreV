package com.sbro.emucorev.ui.cheats

import android.content.Context
import com.sbro.emucorev.core.CheatBridge
import com.sbro.emucorev.core.VitaCheatSnapshot
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL

data class CheatCatalogEntry(
    val id: String,
    val titleId: String,
    val title: String,
    val region: String,
    val version: String,
    val authors: String,
    val description: String,
    val blockCount: Int,
    val downloadUrl: String,
    val sourceUrl: String
)

class CheatCatalogRepository(private val context: Context) {

    companion object {
        private const val CACHE_TTL_MS = 6 * 60 * 60 * 1000L
        private const val MAX_CATALOG_BYTES = 8 * 1024 * 1024
        private const val MAX_PACK_BYTES = 2 * 1024 * 1024

        private val CATALOG_URLS = listOf(
            "https://raw.githubusercontent.com/sashkinbro/EmuCoreV-Cheat/main/cheats.json",
            "https://github.com/sashkinbro/EmuCoreV-Cheat/raw/main/cheats.json",
            "https://cdn.jsdelivr.net/gh/sashkinbro/EmuCoreV-Cheat@main/cheats.json"
        )
    }

    private val cacheFile: File
        get() = File(context.filesDir, "remote-content/cheats-v1.json")

    fun cached(): List<CheatCatalogEntry> {
        val file = cacheFile
        if (!file.isFile) return emptyList()
        return parse(file.readText())
    }

    suspend fun load(force: Boolean = false): List<CheatCatalogEntry> = withContext(Dispatchers.IO) {
        val file = cacheFile
        val fresh = file.isFile && System.currentTimeMillis() - file.lastModified() < CACHE_TTL_MS
        if (!force && fresh) return@withContext parse(file.readText())

        for (url in CATALOG_URLS) {
            val body = runCatching { httpGetText(url, MAX_CATALOG_BYTES) }.getOrNull() ?: continue
            val entries = parse(body)
            if (entries.isEmpty()) continue
            runCatching {
                file.parentFile?.mkdirs()
                file.writeText(body)
            }
            return@withContext entries
        }

        if (file.isFile) parse(file.readText()) else emptyList()
    }

    suspend fun download(entry: CheatCatalogEntry): VitaCheatSnapshot? = withContext(Dispatchers.IO) {
        val bytes = downloadPack(entry) ?: return@withContext null

        val temp = File(context.cacheDir, "cheat_download.tmp")
        temp.writeBytes(bytes)
        val snapshot = CheatBridge.importFile(entry.titleId, temp.absolutePath, "${entry.titleId}.psv")
        temp.delete()
        snapshot.takeIf { it.cheats.isNotEmpty() }
    }

    private fun downloadPack(entry: CheatCatalogEntry): ByteArray? {
        val path = entry.downloadUrl.substringAfter("/EmuCoreV-Cheat/main/", "files/${entry.titleId}.psv")
        val urls = listOf(
            entry.downloadUrl,
            "https://github.com/sashkinbro/EmuCoreV-Cheat/raw/main/$path",
            "https://cdn.jsdelivr.net/gh/sashkinbro/EmuCoreV-Cheat@main/$path"
        )
        for (url in urls) {
            val bytes = runCatching { httpGetBytes(url, MAX_PACK_BYTES) }.getOrNull()
            if (bytes != null && bytes.isNotEmpty()) return bytes
        }
        return null
    }

    private fun parse(body: String): List<CheatCatalogEntry> = runCatching {
        val root = JSONObject(body)
        val array = root.optJSONArray("entries") ?: return emptyList()
        buildList {
            for (index in 0 until array.length()) {
                val item = array.optJSONObject(index) ?: continue
                val titleId = item.optString("titleId").uppercase()
                val downloadUrl = item.optString("downloadUrl")
                if (titleId.isBlank() || !downloadUrl.startsWith("https://")) continue
                add(
                    CheatCatalogEntry(
                        id = item.optString("id").ifBlank { titleId },
                        titleId = titleId,
                        title = item.optString("title"),
                        region = item.optString("region"),
                        version = item.optString("version"),
                        authors = item.optString("authors"),
                        description = item.optString("description"),
                        blockCount = item.optInt("blockCount"),
                        downloadUrl = downloadUrl,
                        sourceUrl = item.optString("sourceUrl")
                    )
                )
            }
        }
    }.getOrDefault(emptyList())

    private fun httpGetText(url: String, maxBytes: Int): String {
        val bytes = httpGetBytes(url, maxBytes)
        return bytes.toString(Charsets.UTF_8).removePrefix("\uFEFF")
    }

    private fun httpGetBytes(url: String, maxBytes: Int): ByteArray {
        val connection = (URL(url).openConnection() as HttpURLConnection).apply {
            connectTimeout = 10_000
            readTimeout = 20_000
            instanceFollowRedirects = true
            setRequestProperty("User-Agent", "EmuCoreV")
        }
        try {
            if (connection.responseCode !in 200..299)
                throw IllegalStateException("HTTP ${connection.responseCode}")
            val stream = connection.inputStream
            val buffer = ByteArray(16 * 1024)
            val output = java.io.ByteArrayOutputStream()
            while (true) {
                val read = stream.read(buffer)
                if (read < 0) break
                output.write(buffer, 0, read)
                if (output.size() > maxBytes)
                    throw IllegalStateException("Response too large")
            }
            return output.toByteArray()
        } finally {
            connection.disconnect()
        }
    }
}
