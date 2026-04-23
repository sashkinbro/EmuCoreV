package com.sbro.emucorev.data

import android.content.Context
import android.util.Xml
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.InputStream
import java.text.Normalizer
import java.net.HttpURLConnection
import java.net.URL
import java.util.Properties
import java.util.zip.ZipInputStream
import org.json.JSONObject

class VitaCompatibilityRepository(context: Context) {
    private val appContext = context.applicationContext
    private val compatibilityDir = File(appContext.filesDir, "compatibility")
    private val xmlFile = File(compatibilityDir, "app_compat_db.xml")
    private val jsonFile = File(compatibilityDir, "commercial_list.json")
    private val metaFile = File(compatibilityDir, "app_compat_meta.properties")

    suspend fun getSnapshot(): VitaCompatibilitySnapshot {
        return kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
            loadSnapshot()
        }
    }

    private fun loadSnapshot(): VitaCompatibilitySnapshot {
        synchronized(lock) {
            cachedSnapshot?.let { snapshot ->
                if (!snapshot.shouldRefresh()) return snapshot
            }
        }

        compatibilityDir.mkdirs()
        val localMeta = readMeta()
        val localSnapshot = loadLocalSnapshot(localMeta)
        val shouldRefresh = localSnapshot == null || (System.currentTimeMillis() - localMeta.lastCheckedAtMs) >= refreshIntervalMs
        val refreshedSnapshot = if (shouldRefresh) {
            downloadLatestSnapshot() ?: localSnapshot?.also { writeMeta(localMeta.copy(lastCheckedAtMs = System.currentTimeMillis(), dbUpdatedAt = it.databaseUpdatedAt)) }
        } else {
            null
        }

        val resolved = refreshedSnapshot ?: localSnapshot ?: VitaCompatibilitySnapshot.EMPTY
        synchronized(lock) {
            cachedSnapshot = resolved
        }
        return resolved
    }

    private fun loadLocalSnapshot(meta: CompatibilityMeta): VitaCompatibilitySnapshot? {
        if ((!xmlFile.exists() || xmlFile.length() <= 0L) && (!jsonFile.exists() || jsonFile.length() <= 0L)) return null
        return runCatching {
            val checkedAtMs = meta.lastCheckedAtMs.takeIf { it > 0 }
                ?: maxOf(xmlFile.lastModified(), jsonFile.lastModified())
            val xmlSnapshot = if (xmlFile.exists() && xmlFile.length() > 0L) {
                FileInputStream(xmlFile).use { input ->
                    parseXml(
                        input = input,
                        checkedAtMs = checkedAtMs,
                        fallbackUpdatedAt = meta.dbUpdatedAt
                    )
                }
            } else {
                VitaCompatibilitySnapshot.EMPTY.copy(checkedAtMs = checkedAtMs)
            }
            mergeWebsiteIndex(
                snapshot = xmlSnapshot,
                jsonInput = jsonFile.takeIf { it.exists() && it.length() > 0L }?.inputStream()
            )
        }.getOrNull()
    }

    private fun downloadLatestSnapshot(): VitaCompatibilitySnapshot? {
        val zipFile = File(compatibilityDir, "app_compat_db.tmp.zip")
        val tempXmlFile = File(compatibilityDir, "app_compat_db.tmp.xml")
        val tempJsonFile = File(compatibilityDir, "commercial_list.tmp.json")
        val checkedAtMs = System.currentTimeMillis()

        return runCatching {
            downloadZip(zipFile)
            extractXml(zipFile, tempXmlFile)
            runCatching { downloadJson(tempJsonFile) }
            val snapshot = FileInputStream(tempXmlFile).use { input ->
                parseXml(input, checkedAtMs = checkedAtMs)
            }
            val mergedSnapshot = mergeWebsiteIndex(
                snapshot = snapshot,
                jsonInput = tempJsonFile.takeIf { it.exists() && it.length() > 0L }?.inputStream()
            )
            if (xmlFile.exists()) {
                xmlFile.delete()
            }
            if (!tempXmlFile.renameTo(xmlFile)) {
                tempXmlFile.copyTo(xmlFile, overwrite = true)
                tempXmlFile.delete()
            }
            if (tempJsonFile.exists() && tempJsonFile.length() > 0L) {
                if (jsonFile.exists()) {
                    jsonFile.delete()
                }
                if (!tempJsonFile.renameTo(jsonFile)) {
                    tempJsonFile.copyTo(jsonFile, overwrite = true)
                    tempJsonFile.delete()
                }
            }
            writeMeta(CompatibilityMeta(lastCheckedAtMs = checkedAtMs, dbUpdatedAt = mergedSnapshot.databaseUpdatedAt))
            mergedSnapshot
        }.getOrNull().also {
            zipFile.delete()
            tempXmlFile.delete()
            tempJsonFile.delete()
        }
    }

    private fun downloadZip(target: File) {
        val connection = openConnection(compatibilityZipUrl)
        connection.inputStream.use { input ->
            FileOutputStream(target).use { output -> input.copyTo(output) }
        }
        if (connection.responseCode !in 200..299) {
            error("Compatibility download failed with HTTP ${connection.responseCode}")
        }
    }

    private fun downloadJson(target: File) {
        val connection = openConnection(compatibilityApiUrl)
        connection.inputStream.use { input ->
            FileOutputStream(target).use { output -> input.copyTo(output) }
        }
        if (connection.responseCode !in 200..299) {
            error("Compatibility API download failed with HTTP ${connection.responseCode}")
        }
    }

    private fun openConnection(url: String): HttpURLConnection {
        return (URL(url).openConnection() as HttpURLConnection).apply {
            connectTimeout = 15_000
            readTimeout = 30_000
            instanceFollowRedirects = true
            requestMethod = "GET"
            setRequestProperty("Accept", "*/*")
        }
    }

    private fun extractXml(zipFile: File, targetXml: File) {
        ZipInputStream(FileInputStream(zipFile)).use { zip ->
            var entry = zip.nextEntry
            while (entry != null) {
                if (!entry.isDirectory && entry.name.endsWith(".xml", ignoreCase = true)) {
                    FileOutputStream(targetXml).use { output -> zip.copyTo(output) }
                    return
                }
                zip.closeEntry()
                entry = zip.nextEntry
            }
        }
        error("Compatibility archive does not contain XML")
    }

    private fun parseXml(
        input: InputStream,
        checkedAtMs: Long,
        fallbackUpdatedAt: String? = null
    ): VitaCompatibilitySnapshot {
        val parser = Xml.newPullParser().apply {
            setFeature("http://xmlpull.org/v1/doc/features.html#process-namespaces", false)
            setInput(input, "UTF-8")
        }
        val entries = linkedMapOf<String, VitaCompatibilityRecord>()
        var databaseUpdatedAt = fallbackUpdatedAt
        var currentApp: MutableCompatibilityRecord? = null
        var currentValueTag: String? = null
        var insideLabels = false
        var eventType = parser.eventType

        while (eventType != org.xmlpull.v1.XmlPullParser.END_DOCUMENT) {
            when (eventType) {
                org.xmlpull.v1.XmlPullParser.START_TAG -> {
                    when {
                        parser.depth == 1 && parser.name == "compatibility" -> {
                            databaseUpdatedAt = parser.getAttributeValue(null, "iso_db_updated_at") ?: fallbackUpdatedAt
                        }

                        parser.depth == 2 -> {
                            currentApp = MutableCompatibilityRecord(
                                titleId = parser.getAttributeValue(null, "title_id").orEmpty()
                            )
                        }

                        parser.depth >= 3 && parser.name == "labels" -> {
                            insideLabels = true
                            currentValueTag = null
                        }

                        parser.depth >= 3 -> {
                            currentValueTag = parser.name
                        }
                    }
                }

                org.xmlpull.v1.XmlPullParser.TEXT -> {
                    val value = parser.text?.trim().orEmpty()
                    if (value.isNotEmpty()) {
                        if (insideLabels) {
                            currentApp?.labelIds?.add(value.toLongOrNull() ?: -1L)
                        } else {
                            when (currentValueTag) {
                                "issue_id" -> currentApp?.issueId = value.toIntOrNull() ?: 0
                                "updated_at" -> currentApp?.updatedAtEpochSeconds = value.toLongOrNull()
                            }
                        }
                    }
                }

                org.xmlpull.v1.XmlPullParser.END_TAG -> {
                    when {
                        parser.depth == 2 -> {
                            currentApp?.build()?.takeIf { isSupportedTitleId(it.titleId) }?.let { record ->
                                entries[record.titleId] = record
                            }
                            currentApp = null
                            insideLabels = false
                            currentValueTag = null
                        }

                        parser.name == "labels" -> {
                            insideLabels = false
                            currentValueTag = null
                        }

                        parser.depth >= 3 -> currentValueTag = null
                    }
                }
            }
            eventType = parser.next()
        }

        return VitaCompatibilitySnapshot(
            records = entries,
            databaseUpdatedAt = databaseUpdatedAt,
            checkedAtMs = checkedAtMs,
            normalizedNameIndex = emptyMap()
        )
    }

    private fun mergeWebsiteIndex(
        snapshot: VitaCompatibilitySnapshot,
        jsonInput: InputStream?
    ): VitaCompatibilitySnapshot {
        if (jsonInput == null) return snapshot
        return jsonInput.use { input ->
            val root = JSONObject(input.bufferedReader().readText())
            val list = root.optJSONArray("list")
            val dateEpochSeconds = root.optLong("date")
            val records = snapshot.records.toMutableMap()
            val normalizedNameIndex = linkedMapOf<String, MutableList<VitaCompatibilityRecord>>()

            if (list != null) {
                for (index in 0 until list.length()) {
                    val item = list.optJSONObject(index) ?: continue
                    val titleId = item.optString("titleId").trim().uppercase()
                    val name = item.optString("name").trim()
                    if (titleId.isBlank() || name.isBlank()) continue
                    val record = records[titleId] ?: VitaCompatibilityRecord(
                        titleId = titleId,
                        issueId = item.optInt("issueId"),
                        updatedAtEpochSeconds = null,
                        state = item.optString("status").toCompatibilityState()
                    ).also { fallback -> records[titleId] = fallback }
                    normalizedNameIndex.getOrPut(normalizeName(name)) { mutableListOf() }.add(record)
                }
            }

            snapshot.copy(
                records = records,
                databaseUpdatedAt = snapshot.databaseUpdatedAt ?: dateEpochSeconds.takeIf { it > 0 }?.let(::formatEpochSecond),
                normalizedNameIndex = normalizedNameIndex.mapValues { (_, value) -> value.distinctBy(VitaCompatibilityRecord::titleId) }
            )
        }
    }

    private fun isSupportedTitleId(titleId: String): Boolean {
        return titleId == "NPXS10007" || titleId.contains("PCS")
    }

    private fun normalizeName(value: String): String {
        return Normalizer.normalize(value, Normalizer.Form.NFKC)
            .lowercase()
            .replace("™", "")
            .replace("®", "")
            .replace("©", "")
            .replace("\\s+".toRegex(), " ")
            .trim()
    }

    private fun readMeta(): CompatibilityMeta {
        if (!metaFile.exists()) return CompatibilityMeta()
        return runCatching {
            FileInputStream(metaFile).use { input ->
                Properties().apply { load(input) }
            }
        }.map { properties ->
            CompatibilityMeta(
                lastCheckedAtMs = properties.getProperty("last_checked_at_ms")?.toLongOrNull() ?: 0L,
                dbUpdatedAt = properties.getProperty("db_updated_at")
            )
        }.getOrDefault(CompatibilityMeta())
    }

    private fun writeMeta(meta: CompatibilityMeta) {
        runCatching {
            FileOutputStream(metaFile).use { output ->
                Properties().apply {
                    setProperty("last_checked_at_ms", meta.lastCheckedAtMs.toString())
                    meta.dbUpdatedAt?.let { setProperty("db_updated_at", it) }
                    store(output, null)
                }
            }
        }
    }

    private data class CompatibilityMeta(
        val lastCheckedAtMs: Long = 0L,
        val dbUpdatedAt: String? = null
    )

    private data class MutableCompatibilityRecord(
        val titleId: String,
        var issueId: Int = 0,
        var updatedAtEpochSeconds: Long? = null,
        val labelIds: MutableList<Long> = mutableListOf()
    ) {
        fun build(): VitaCompatibilityRecord {
            return VitaCompatibilityRecord(
                titleId = titleId,
                issueId = issueId,
                updatedAtEpochSeconds = updatedAtEpochSeconds,
                state = labelIds.toCompatibilityState()
            )
        }
    }

    companion object {
        private const val compatibilityZipUrl =
            "https://github.com/Vita3K/compatibility/releases/download/compat_db/app_compat_db.xml.zip"
        private const val compatibilityApiUrl = "https://vita3k-api.pedro.moe/list/commercial"
        private const val refreshIntervalMs = 12L * 60L * 60L * 1000L
        private val lock = Any()

        @Volatile
        private var cachedSnapshot: VitaCompatibilitySnapshot? = null

        private const val labelNothing = 1260231569L
        private const val labelBootable = 1344750319L
        private const val labelIntro = 1260231381L
        private const val labelMenu = 1344751053L
        private const val labelIngameLess = 1344752299L
        private const val labelIngameMore = 1260231985L
        private const val labelPlayable = 920344019L

        private fun List<Long>.toCompatibilityState(): VitaCompatibilityState {
            return when {
                contains(labelPlayable) -> VitaCompatibilityState.PLAYABLE
                contains(labelIngameMore) -> VitaCompatibilityState.INGAME_MORE
                contains(labelIngameLess) -> VitaCompatibilityState.INGAME_LESS
                contains(labelMenu) -> VitaCompatibilityState.MENU
                contains(labelIntro) -> VitaCompatibilityState.INTRO
                contains(labelBootable) -> VitaCompatibilityState.BOOTABLE
                contains(labelNothing) -> VitaCompatibilityState.NOTHING
                else -> VitaCompatibilityState.UNKNOWN
            }
        }

        private fun String.toCompatibilityState(): VitaCompatibilityState {
            return when (trim()) {
                "Nothing" -> VitaCompatibilityState.NOTHING
                "Bootable" -> VitaCompatibilityState.BOOTABLE
                "Intro" -> VitaCompatibilityState.INTRO
                "Menu" -> VitaCompatibilityState.MENU
                "Ingame -" -> VitaCompatibilityState.INGAME_LESS
                "Ingame +" -> VitaCompatibilityState.INGAME_MORE
                "Playable" -> VitaCompatibilityState.PLAYABLE
                else -> VitaCompatibilityState.UNKNOWN
            }
        }

        private fun formatEpochSecond(epochSeconds: Long): String {
            return java.time.Instant.ofEpochSecond(epochSeconds)
                .atZone(java.time.ZoneOffset.UTC)
                .toLocalDateTime()
                .toString()
                .replace('T', ' ')
        }
    }
}

data class VitaCompatibilitySnapshot(
    val records: Map<String, VitaCompatibilityRecord>,
    val databaseUpdatedAt: String?,
    val checkedAtMs: Long,
    val normalizedNameIndex: Map<String, List<VitaCompatibilityRecord>> = emptyMap()
) {
    fun resolve(titleId: String?): VitaCompatibilitySummary? {
        val normalizedTitleId = titleId?.trim()?.uppercase()?.takeIf(String::isNotBlank) ?: return null
        val record = records[normalizedTitleId] ?: return null
        return VitaCompatibilitySummary(
            matchedTitleId = record.titleId,
            issueId = record.issueId,
            state = record.state,
            updatedAtEpochSeconds = record.updatedAtEpochSeconds
        )
    }

    fun resolve(titleIds: List<String>, gameName: String? = null): VitaCompatibilitySummary? {
        val normalizedIds = titleIds
            .map(String::trim)
            .filter(String::isNotBlank)
            .map(String::uppercase)
            .distinct()
        val record = normalizedIds.firstNotNullOfOrNull(records::get)
            ?: gameName
                ?.let(::normalizeCompatibilityName)
                ?.let(normalizedNameIndex::get)
                ?.takeIf { it.size == 1 }
                ?.firstOrNull()
            ?: return null
        return VitaCompatibilitySummary(
            matchedTitleId = record.titleId,
            issueId = record.issueId,
            state = record.state,
            updatedAtEpochSeconds = record.updatedAtEpochSeconds,
            candidateTitleIds = normalizedIds.ifEmpty { listOf(record.titleId) }
        )
    }

    fun shouldRefresh(): Boolean {
        return this !== EMPTY && (System.currentTimeMillis() - checkedAtMs) >= 12L * 60L * 60L * 1000L
    }

    companion object {
        val EMPTY = VitaCompatibilitySnapshot(
            records = emptyMap(),
            databaseUpdatedAt = null,
            checkedAtMs = 0L,
            normalizedNameIndex = emptyMap()
        )

        private fun normalizeCompatibilityName(value: String): String {
            return Normalizer.normalize(value, Normalizer.Form.NFKC)
                .lowercase()
                .replace("™", "")
                .replace("®", "")
                .replace("©", "")
                .replace("\\s+".toRegex(), " ")
                .trim()
        }
    }
}

data class VitaCompatibilityRecord(
    val titleId: String,
    val issueId: Int,
    val updatedAtEpochSeconds: Long?,
    val state: VitaCompatibilityState
)
