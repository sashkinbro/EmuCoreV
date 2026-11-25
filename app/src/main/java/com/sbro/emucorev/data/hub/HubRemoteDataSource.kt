package com.sbro.emucorev.data.hub

import android.content.Context
import com.sbro.emucorev.BuildConfig
import com.sbro.emucorev.R
import com.sbro.emucorev.data.store.StoreCatalog
import com.sbro.emucorev.data.store.StoreFile
import kotlinx.serialization.json.Json
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.KeyFactory
import java.security.MessageDigest
import java.security.Signature
import java.security.spec.X509EncodedKeySpec
import java.util.Base64

data class HubRemoteSnapshot(
    val channel: HubChannel,
    val manifest: HubManifest,
    val locale: String,
    val requestedLocale: String,
    val baseUrl: String,
    val items: List<HubIndexItem>,
    val assets: List<HubAsset>,
    val themes: HubThemeCatalog?,
    val store: StoreCatalog? = null
)

class HubRemoteDataSource(context: Context) {
    private val appContext = context.applicationContext
    private val json = Json {
        ignoreUnknownKeys = true
        explicitNulls = false
    }

    fun load(requestedLocale: String): HubRemoteSnapshot {
        val channelBytes = fetchBytes(CHANNEL_URL, MAX_CHANNEL_BYTES)
        val signatureBytes = fetchBytes(CHANNEL_SIGNATURE_URL, MAX_SIGNATURE_BYTES)
            .toString(Charsets.UTF_8)
            .trim()
            .let { Base64.getDecoder().decode(it) }
        require(verifyChannel(channelBytes, signatureBytes)) { "Hub channel signature is invalid" }

        val channel = json.decodeFromString<HubChannel>(channelBytes.toString(Charsets.UTF_8))
        require(channel.formatVersion == SUPPORTED_CHANNEL_FORMAT) { "Unsupported Hub channel format" }
        require(channel.channel == "stable") { "Unexpected Hub channel" }
        require(channel.commit.matches(Regex("[0-9a-f]{40}"))) { "Invalid Hub commit" }
        require(channel.minimumClientVersionCode <= HUB_PROTOCOL_CLIENT_VERSION_CODE) { "A newer Hub client is required" }
        val manifestUrl = channel.manifest.url ?: error("Hub manifest URL is missing")
        requirePinnedManifestUrl(manifestUrl, channel.commit)
        val manifestBytes = fetchVerified(manifestUrl, channel.manifest, MAX_MANIFEST_BYTES)
        val manifest = json.decodeFromString<HubManifest>(manifestBytes.toString(Charsets.UTF_8))
        require(manifest.formatVersion == SUPPORTED_CATALOG_FORMAT) { "Unsupported Hub catalog format" }
        require(manifest.releaseId == channel.releaseId) { "Hub release identifiers do not match" }
        require(manifest.catalogRevision == channel.catalogRevision) { "Hub catalog revisions do not match" }
        require(manifest.minimumClientVersionCode <= HUB_PROTOCOL_CLIENT_VERSION_CODE) { "A newer Hub client is required" }

        val resolvedLocale = HubLocaleResolver.resolve(requestedLocale, manifest.supportedLocales)
        val manifestSuffix = "catalog/v1/manifest.json"
        require(manifestUrl.endsWith(manifestSuffix)) { "Unexpected Hub manifest path" }
        val baseUrl = manifestUrl.removeSuffix(manifestSuffix)
        val items = HubKind.entries.flatMap { kind ->
            val section = manifest.indexes[kind.wireValue]
                ?: if (kind == HubKind.MANUALS) return@flatMap emptyList() else error("Hub ${kind.wireValue} index is missing")
            val references = section[resolvedLocale] ?: section[manifest.defaultLocale]
                ?: error("Hub locale index is missing")
            references.flatMap { reference ->
                val url = resolvePinnedUrl(baseUrl, reference.path)
                val bytes = fetchVerified(url, reference, MAX_INDEX_BYTES)
                val page = json.decodeFromString<HubIndexPage>(bytes.toString(Charsets.UTF_8))
                require(page.formatVersion == SUPPORTED_CATALOG_FORMAT)
                require(page.releaseId == manifest.releaseId)
                require(page.catalogRevision == manifest.catalogRevision)
                require(page.kind == kind.wireValue)
                require(page.locale == resolvedLocale || page.locale == manifest.defaultLocale)
                page.items
            }
        }.distinctBy(HubIndexItem::id)

        val assetsUrl = resolvePinnedUrl(baseUrl, manifest.assets.path)
        val assetBytes = fetchVerified(assetsUrl, manifest.assets, MAX_ASSET_CATALOG_BYTES)
        val assets = json.decodeFromString<HubAssetCatalog>(assetBytes.toString(Charsets.UTF_8))
        require(assets.releaseId == manifest.releaseId)

        // Optional section: a broken theme catalog must not take the rest of the Hub down.
        val themes = manifest.themes?.let { reference ->
            runCatching {
                val themesUrl = resolvePinnedUrl(baseUrl, reference.path)
                val bytes = fetchVerified(themesUrl, reference, MAX_THEME_CATALOG_BYTES)
                json.decodeFromString<HubThemeCatalog>(bytes.toString(Charsets.UTF_8)).also { catalog ->
                    require(catalog.formatVersion == SUPPORTED_THEME_CATALOG_FORMAT)
                    require(catalog.releaseId == manifest.releaseId)
                    catalog.themes.forEach { entry ->
                        resolvePinnedUrl(baseUrl, entry.preview.path)
                        resolvePinnedUrl(baseUrl, entry.packageFile.path)
                    }
                }
            }.getOrNull()
        }

        // Optional section as well: the Store catalog never breaks the Hub.
        val store = manifest.store?.let { reference ->
            runCatching {
                val storeUrl = resolvePinnedUrl(baseUrl, reference.path)
                val bytes = fetchVerified(storeUrl, reference, MAX_STORE_CATALOG_BYTES)
                json.decodeFromString<StoreCatalog>(bytes.toString(Charsets.UTF_8)).also { catalog ->
                    require(catalog.formatVersion == SUPPORTED_STORE_CATALOG_FORMAT)
                    require(catalog.releaseId == manifest.releaseId)
                    catalog.items.forEach { item ->
                        resolvePinnedUrl(baseUrl, item.cover.path)
                        item.gallery.forEach { resolvePinnedUrl(baseUrl, it.path) }
                        resolvePinnedUrl(baseUrl, item.packageFile.path)
                    }
                }
            }.onFailure { android.util.Log.w("EmuCoreStore", "Store catalog rejected", it) }.getOrNull()
        }

        return HubRemoteSnapshot(
            channel = channel,
            manifest = manifest,
            locale = resolvedLocale,
            requestedLocale = HubLocaleResolver.resolve(requestedLocale),
            baseUrl = baseUrl,
            items = items,
            assets = assets.assets,
            themes = themes,
            store = store
        )
    }

    fun fetchDocument(reference: HubFileReference): ByteArray {
        val url = reference.url ?: error("Hub document URL is missing")
        return fetchVerified(url, reference, MAX_DOCUMENT_BYTES)
    }

    fun fetchAsset(url: String, sha256: String, bytes: Long): ByteArray = fetchVerified(
        url = url,
        reference = HubFileReference(url = url, sha256 = sha256, bytes = bytes, contentType = "application/octet-stream"),
        hardLimit = MAX_IMAGE_BYTES
    )

    fun fetchThemePackage(url: String, file: HubThemeFile): ByteArray = fetchVerified(
        url = url,
        reference = HubFileReference(url = url, sha256 = file.sha256, bytes = file.bytes, contentType = "application/zip"),
        hardLimit = MAX_THEME_PACKAGE_BYTES
    )

    /**
     * Streams a pinned Store file into [target] while hashing it, reporting progress in 0..1.
     * Size and SHA-256 must match [file]; [target] is deleted on any failure. Blocking; [isActive]
     * is polled so the caller's coroutine cancellation stops the transfer.
     */
    fun downloadVerified(
        url: String,
        file: StoreFile,
        hardLimit: Long,
        target: File,
        isActive: () -> Boolean,
        onProgress: (Float) -> Unit
    ) {
        require(file.bytes in 1..hardLimit) { "Store file size is invalid" }
        require(file.sha256.matches(Regex("[0-9a-f]{64}"))) { "Store checksum is invalid" }
        try {
            var next = URL(url)
            var done = false
            repeat(MAX_REDIRECTS + 1) {
                if (done) return@repeat
                require(next.protocol == "https" && next.host == "raw.githubusercontent.com") { "Unexpected Store host" }
                val connection = (next.openConnection() as HttpURLConnection).apply {
                    connectTimeout = CONNECT_TIMEOUT_MS
                    readTimeout = READ_TIMEOUT_MS
                    instanceFollowRedirects = false
                    setRequestProperty("Accept", "application/octet-stream, image/*")
                    setRequestProperty("User-Agent", "EmuCoreV-Hub/${BuildConfig.VERSION_NAME}")
                }
                try {
                    val response = connection.responseCode
                    if (response in 300..399) {
                        val location = connection.getHeaderField("Location") ?: throw IOException("Store redirect is missing a location")
                        next = URL(next, location)
                        return@repeat
                    }
                    if (response !in 200..299) throw IOException("Store request failed with HTTP $response")
                    val declared = connection.contentLengthLong
                    if (declared > file.bytes) throw IOException("Store response is too large")
                    val digest = MessageDigest.getInstance("SHA-256")
                    var total = 0L
                    connection.inputStream.buffered().use { input ->
                        target.outputStream().buffered().use { output ->
                            val buffer = ByteArray(DEFAULT_BUFFER_SIZE * 2)
                            while (true) {
                                if (!isActive()) throw java.util.concurrent.CancellationException("Store download cancelled")
                                val read = input.read(buffer)
                                if (read < 0) break
                                total += read
                                if (total > file.bytes) throw IOException("Store response is too large")
                                digest.update(buffer, 0, read)
                                output.write(buffer, 0, read)
                                onProgress((total.toFloat() / file.bytes).coerceIn(0f, 1f))
                            }
                        }
                    }
                    require(total == file.bytes) { "Store file size does not match" }
                    require(digest.digest().toHex() == file.sha256) { "Store file checksum does not match" }
                    onProgress(1f)
                    done = true
                } finally {
                    connection.disconnect()
                }
            }
            if (!done) throw IOException("Too many Store redirects")
        } catch (error: Throwable) {
            target.delete()
            throw error
        }
    }

    private fun fetchVerified(url: String, reference: HubFileReference, hardLimit: Long): ByteArray {
        require(reference.bytes in 1..hardLimit) { "Hub file size is invalid" }
        require(reference.sha256.matches(Regex("[0-9a-f]{64}"))) { "Hub checksum is invalid" }
        val bytes = fetchBytes(url, hardLimit)
        require(bytes.size.toLong() == reference.bytes) { "Hub file size does not match" }
        val digest = MessageDigest.getInstance("SHA-256").digest(bytes).toHex()
        require(digest == reference.sha256) { "Hub file checksum does not match" }
        return bytes
    }

    private fun fetchBytes(rawUrl: String, maxBytes: Long): ByteArray {
        var next = URL(rawUrl)
        repeat(MAX_REDIRECTS + 1) {
            require(next.protocol == "https") { "Hub downloads require HTTPS" }
            val connection = (next.openConnection() as HttpURLConnection).apply {
                connectTimeout = CONNECT_TIMEOUT_MS
                readTimeout = READ_TIMEOUT_MS
                instanceFollowRedirects = false
                setRequestProperty("Accept", "application/json, text/plain, image/*")
                setRequestProperty("User-Agent", "EmuCoreV-Hub/${BuildConfig.VERSION_NAME}")
            }
            try {
                val response = connection.responseCode
                if (response in 300..399) {
                    val location = connection.getHeaderField("Location") ?: throw IOException("Hub redirect is missing a location")
                    next = URL(next, location)
                    return@repeat
                }
                if (response !in 200..299) throw IOException("Hub request failed with HTTP $response")
                val declaredLength = connection.contentLengthLong
                if (declaredLength > maxBytes) throw IOException("Hub response is too large")
                connection.inputStream.buffered().use { input ->
                    val output = ByteArrayOutputStream()
                    val buffer = ByteArray(DEFAULT_BUFFER_SIZE * 2)
                    var total = 0L
                    while (true) {
                        val read = input.read(buffer)
                        if (read < 0) break
                        total += read
                        if (total > maxBytes) throw IOException("Hub response is too large")
                        output.write(buffer, 0, read)
                    }
                    return output.toByteArray()
                }
            } finally {
                connection.disconnect()
            }
        }
        throw IOException("Too many Hub redirects")
    }

    private fun verifyChannel(bytes: ByteArray, signatureBytes: ByteArray): Boolean = runCatching {
        val pem = appContext.resources.openRawResource(R.raw.hub_channel_public_key)
            .bufferedReader()
            .use { it.readText() }
        val der = pem.lineSequence()
            .filterNot { it.startsWith("-----") }
            .joinToString("")
            .let { Base64.getDecoder().decode(it) }
        val publicKey = KeyFactory.getInstance("EC").generatePublic(X509EncodedKeySpec(der))
        Signature.getInstance("SHA256withECDSA").run {
            initVerify(publicKey)
            update(bytes)
            verify(signatureBytes)
        }
    }.getOrDefault(false)

    private fun requirePinnedManifestUrl(url: String, commit: String) {
        val parsed = URL(url)
        require(parsed.protocol == "https")
        require(parsed.host == "raw.githubusercontent.com")
        require(parsed.path == "/sashkinbro/EmuCoreX-News-Media-Hub/$commit/catalog/v1/manifest.json")
    }

    private fun resolvePinnedUrl(baseUrl: String, relativePath: String?): String {
        require(!relativePath.isNullOrBlank()) { "Hub path is missing" }
        require(relativePath.matches(Regex("[A-Za-z0-9._/-]+"))) { "Hub path contains invalid characters" }
        require(!relativePath.startsWith('/') && !relativePath.contains("..") && !relativePath.contains('\\')) { "Unsafe Hub path" }
        val resolved = URL(URL(baseUrl), relativePath)
        require(resolved.protocol == "https" && resolved.host == "raw.githubusercontent.com") { "Unexpected Hub host" }
        return resolved.toString()
    }

    companion object {
        /** Hub protocol level this client implements; not the app versionCode. */
        private const val HUB_PROTOCOL_CLIENT_VERSION_CODE = 166
        const val CHANNEL_URL = "https://raw.githubusercontent.com/sashkinbro/EmuCoreX-News-Media-Hub/refs/heads/main/channels/stable-v1.json"
        const val CHANNEL_SIGNATURE_URL = "https://raw.githubusercontent.com/sashkinbro/EmuCoreX-News-Media-Hub/refs/heads/main/channels/stable-v1.sig"
        private const val SUPPORTED_CHANNEL_FORMAT = 1
        private const val SUPPORTED_CATALOG_FORMAT = 1
        private const val MAX_REDIRECTS = 4
        private const val CONNECT_TIMEOUT_MS = 8_000
        private const val READ_TIMEOUT_MS = 15_000
        private const val MAX_CHANNEL_BYTES = 64L * 1024L
        private const val MAX_SIGNATURE_BYTES = 4L * 1024L
        private const val MAX_MANIFEST_BYTES = 2L * 1024L * 1024L
        private const val MAX_INDEX_BYTES = 2L * 1024L * 1024L
        private const val MAX_ASSET_CATALOG_BYTES = 2L * 1024L * 1024L
        private const val MAX_DOCUMENT_BYTES = 512L * 1024L
        private const val MAX_IMAGE_BYTES = 8L * 1024L * 1024L
        private const val MAX_THEME_CATALOG_BYTES = 2L * 1024L * 1024L
        private const val MAX_THEME_PACKAGE_BYTES = 4L * 1024L * 1024L
        private const val SUPPORTED_THEME_CATALOG_FORMAT = 1
        private const val MAX_STORE_CATALOG_BYTES = 16L * 1024L * 1024L // ~1000 items in 18 locales is already >4 MB
        private const val SUPPORTED_STORE_CATALOG_FORMAT = 1
    }
}

private fun ByteArray.toHex(): String = joinToString("") { byte -> "%02x".format(byte.toInt() and 0xff) }
