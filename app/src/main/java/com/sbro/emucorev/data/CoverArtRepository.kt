package com.sbro.emucorev.data

import android.content.Context
import android.content.SharedPreferences
import android.graphics.BitmapFactory
import android.net.Uri
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.withContext

/** App-lifetime cover identity/cache service; never retains an Activity. */
class CoverArtRepository private constructor(context: Context) {
    private val app = context.applicationContext
    private val preferences = app.getSharedPreferences("emucorev_cover_ui", Context.MODE_PRIVATE)
    private val mutableStyle = MutableStateFlow(readStyle())
    val style: StateFlow<VitaCoverStyle> = mutableStyle.asStateFlow()
    private val mutableCacheVersion = MutableStateFlow(0L)
    val cacheVersion: StateFlow<Long> = mutableCacheVersion.asStateFlow()

    private val preferenceListener = SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
        if (key == STYLE_KEY) mutableStyle.value = readStyle()
    }

    private val cache = CoverDiskCache(File(app.cacheDir, "vita-managed-covers-v2"), CoverAssetFetcher { asset, target, limit ->
        download(asset, target, limit)
    }, ::validImageFile)

    private val index: VitaCoverIndex by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        val json = runCatching {
            app.assets.open("catalog/vita_covers.json").bufferedReader().use { reader ->
                val text = StringBuilder()
                val buffer = CharArray(8192)
                while (true) {
                    val count = reader.read(buffer)
                    if (count < 0) break
                    require(text.length + count <= 32 * 1024 * 1024)
                    text.append(buffer, 0, count)
                }
                text.toString()
            }
        }.getOrDefault("")
        VitaCoverIndex.fromJson(json)
    }

    init { preferences.registerOnSharedPreferenceChangeListener(preferenceListener) }

    fun setStyle(value: VitaCoverStyle) {
        mutableStyle.value = value
        preferences.edit().putString(STYLE_KEY, value.name).apply()
    }

    private fun readStyle(): VitaCoverStyle = runCatching {
        VitaCoverStyle.valueOf(preferences.getString(STYLE_KEY, VitaCoverStyle.THREE_D.name).orEmpty())
    }.getOrDefault(VitaCoverStyle.THREE_D)

    suspend fun resolveCover(
        titleId: String? = null,
        igdbId: Long? = null,
        candidatePath: String? = null,
        fallbackIconPath: String? = null,
        styleOverride: VitaCoverStyle? = null
    ): ResolvedVitaCover = withContext(Dispatchers.IO) {
        VitaCoverResolver(index, cache, ::usableLocalArtwork)
            .resolve(titleId, igdbId, candidatePath, fallbackIconPath, styleOverride ?: style.value)
    }

    /** Stable shareable URL without fetching; local custom artwork is never uploaded. */
    suspend fun buildPublicCoverUrl(
        titleId: String? = null,
        igdbId: Long? = null,
        candidatePath: String? = null,
        styleOverride: VitaCoverStyle? = null
    ): String? = withContext(Dispatchers.IO) {
        if (candidatePath != null && !VitaCoverIndex.isManagedRemoteUrl(candidatePath) &&
            !cache.isManagedPath(VitaCoverResolver.localFile(candidatePath)?.path ?: candidatePath) &&
            usableLocalArtwork(candidatePath)) return@withContext null
        index.find(titleId, igdbId, candidatePath, styleOverride ?: style.value)?.url
    }

    suspend fun clearManagedCache(): CoverCacheClearResult = try {
        cache.clear()
    } finally {
        // Also notify after a cancelled/partially completed clear so existing cards
        // cannot retain a disk-backed bitmap whose file may already be gone.
        mutableCacheVersion.update { it + 1 }
    }

    private fun usableLocalArtwork(path: String): Boolean = runCatching {
        val file = VitaCoverResolver.localFile(path)
        if (file != null) return@runCatching validImageFile(file)
        if (!path.startsWith("content://")) return@runCatching false
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        app.contentResolver.openInputStream(Uri.parse(path))?.let { BoundedCoverInputStream(it) }?.use { input ->
            BitmapFactory.decodeStream(input, null, bounds)
            Unit
        } ?: return@runCatching false
        validBounds(bounds)
    }.getOrDefault(false)

    private suspend fun download(asset: VitaCoverAsset, target: File, maxBytes: Long): CoverFetchResult {
        // VitaCoverAsset validates every path and constructs the sole permitted remote host.
        val connection = URL(asset.url).openConnection() as HttpURLConnection
        try {
            connection.connectTimeout = 10_000
            connection.readTimeout = 15_000
            connection.instanceFollowRedirects = false
            connection.setRequestProperty("User-Agent", "EmuCoreV-Covers/2")
            val status = connection.responseCode
            if (status == 404 || status == 410) return CoverFetchResult.NOT_FOUND
            if (status != 200) return CoverFetchResult.RETRYABLE_FAILURE
            val length = connection.contentLengthLong
            if (length == 0L || length > maxBytes) return CoverFetchResult.RETRYABLE_FAILURE
            connection.inputStream.use { input ->
                target.outputStream().use { output ->
                    val buffer = ByteArray(16 * 1024)
                    var bytes = 0L
                    while (true) {
                        currentCoroutineContext().ensureActive()
                        val count = input.read(buffer)
                        if (count < 0) break
                        bytes += count
                        if (bytes > maxBytes) return CoverFetchResult.RETRYABLE_FAILURE
                        output.write(buffer, 0, count)
                    }
                    output.fd.sync()
                }
            }
            return CoverFetchResult.DOWNLOADED
        } finally {
            connection.disconnect()
        }
    }

    companion object {
        private const val STYLE_KEY = "cover_style"
        @Volatile private var instance: CoverArtRepository? = null

        fun get(context: Context): CoverArtRepository = instance ?: synchronized(this) {
            instance ?: CoverArtRepository(context.applicationContext).also { instance = it }
        }

        private fun validImageFile(file: File): Boolean {
            if (!file.isFile || file.length() !in 1..CoverDiskCache.MAX_BYTES) return false
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(file.absolutePath, bounds)
            return validBounds(bounds)
        }

        private fun validBounds(bounds: BitmapFactory.Options): Boolean =
            CoverImagePolicy.validBounds(bounds.outWidth, bounds.outHeight) &&
                bounds.outMimeType?.startsWith("image/") == true
    }
}
