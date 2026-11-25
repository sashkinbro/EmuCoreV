package com.sbro.emucorev.data.store

import android.content.Context
import android.util.Log
import com.sbro.emucorev.data.hub.HubRepository
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.io.File

/**
 * Store catalog access: the verified catalog (persisted for offline use) and verified package downloads.
 * The catalog travels with the signed Hub sync; this class only republishes the `store` section of it.
 */
class StoreRepository private constructor(context: Context) {
    private val appContext = context.applicationContext
    private val hub = HubRepository.get(appContext)
    private val json = Json {
        ignoreUnknownKeys = true
        explicitNulls = false
    }
    private val catalogFile = File(appContext.filesDir, "store-catalog.json")
    private val downloadDirectory = File(appContext.cacheDir, "store-downloads")
    private val _catalog = MutableStateFlow(readSnapshot())

    /** Last verified Store catalog; survives restarts so the Store works offline. */
    val catalog: StateFlow<StoreSnapshot?> = _catalog.asStateFlow()

    /** Syncs the Hub (which carries the catalog) and publishes the result. Offline failure keeps the cache. */
    suspend fun refresh(languageTag: String?, forceRefresh: Boolean = false): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            // The Hub sync cooldown must not keep an empty Store empty: without a catalog, always fetch.
            hub.sync(languageTag, forceRefresh || _catalog.value == null)
            hub.storeSync.value?.let(::publish)
            Result.success(Unit)
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (error: Exception) {
            Log.w("EmuCoreStore", "Store refresh failed", error)
            Result.failure(error)
        }
    }

    /**
     * Streams the item's package into the cache while verifying size and SHA-256 against the catalog, and
     * returns the file (`cacheDir/store-downloads/<id>-<version>.<ext>`). [onProgress] gets 0..1 and is
     * called on an IO thread. Cancelling the calling coroutine stops the transfer and removes the partial file.
     */
    suspend fun download(item: StoreItem, onProgress: (Float) -> Unit): File = withContext(Dispatchers.IO) {
        val snapshot = _catalog.value ?: error("Store catalog is unavailable")
        // Trust the catalog entry, never the caller-supplied copy of the file reference.
        val entry = snapshot.catalog.items.firstOrNull { it.id == item.id && it.version == item.version }
            ?: error("Store item is not in the catalog")
        require(isSafePath(entry.packageFile.path)) { "Unsafe Store path" }
        val url = snapshot.url(entry.packageFile)
        require(url.startsWith(PINNED_PREFIX)) { "Unexpected Store host" }
        downloadDirectory.mkdirs()
        val target = File(downloadDirectory, "${entry.id}-${entry.version}.${extensionOf(entry.kind)}")
        val part = File(downloadDirectory, "${target.name}.part")
        part.delete()
        try {
            hub.downloadStoreFile(url, entry.packageFile, maxPackageBytes(entry.kind), part, onProgress)
            target.delete()
            check(part.renameTo(target)) { "Unable to save the download" }
            target
        } finally {
            part.delete()
        }
    }

    private fun publish(snapshot: StoreSnapshot) {
        val current = _catalog.value
        if (current != null && snapshot.catalogRevision < current.catalogRevision) return
        _catalog.value = snapshot.forThisApp()
        runCatching {
            val temporary = File(catalogFile.parentFile, "${catalogFile.name}.tmp")
            temporary.writeText(
                json.encodeToString(
                    StoredSnapshot.serializer(),
                    StoredSnapshot(snapshot.baseUrl, snapshot.catalogRevision, snapshot.catalog)
                )
            )
            if (!temporary.renameTo(catalogFile)) {
                temporary.copyTo(catalogFile, overwrite = true)
                temporary.delete()
            }
        }
    }

    private fun readSnapshot(): StoreSnapshot? = runCatching {
        if (!catalogFile.isFile) return null
        val stored = json.decodeFromString<StoredSnapshot>(catalogFile.readText())
        require(stored.baseUrl.startsWith(PINNED_PREFIX))
        require(stored.catalog.formatVersion == 1)
        stored.catalog.items.forEach { item ->
            require(isSafePath(item.cover.path) && isSafePath(item.packageFile.path))
            item.gallery.forEach { require(isSafePath(it.path)) }
        }
        StoreSnapshot(stored.baseUrl, stored.catalogRevision, stored.catalog).forThisApp()
    }.getOrNull()

    @Serializable
    private data class StoredSnapshot(
        val baseUrl: String,
        val catalogRevision: Long,
        val catalog: StoreCatalog
    )

    companion object {
        private const val PINNED_PREFIX = "https://raw.githubusercontent.com/"
        private val SAFE_PATH = Regex("[A-Za-z0-9._/-]+")

        /** Download size cap per kind: themes, sounds, controls and fonts 4 MB, wallpapers 8 MB. */
        fun maxPackageBytes(kind: StoreKind): Long = when (kind) {
            StoreKind.WALLPAPER -> 8L * 1024L * 1024L
            StoreKind.THEME, StoreKind.SOUNDS, StoreKind.CONTROLS, StoreKind.FONT -> 4L * 1024L * 1024L
        }

        fun extensionOf(kind: StoreKind): String = when (kind) {
            StoreKind.THEME -> "ecxtheme"
            StoreKind.WALLPAPER -> "ecxwall"
            StoreKind.SOUNDS -> "ecxsounds"
            StoreKind.CONTROLS -> "ecxcontrols"
            StoreKind.FONT -> "ecxfont"
        }

        internal fun isSafePath(path: String): Boolean =
            path.isNotBlank() && SAFE_PATH.matches(path) && !path.startsWith('/') && !path.contains("..")

        @Volatile private var instance: StoreRepository? = null

        fun get(context: Context): StoreRepository = instance ?: synchronized(this) {
            instance ?: StoreRepository(context).also { instance = it }
        }
    }
}
