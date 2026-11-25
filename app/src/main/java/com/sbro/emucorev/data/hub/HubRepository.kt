package com.sbro.emucorev.data.hub

import android.content.Context
import androidx.room.withTransaction
import com.sbro.emucorev.data.store.StoreFile
import com.sbro.emucorev.data.store.StoreSnapshot
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.withContext
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import java.io.File
import java.net.URL
import java.security.MessageDigest

class HubRepository private constructor(context: Context) {
    private val appContext = context.applicationContext
    private val database = HubDatabase.get(appContext)
    private val dao = database.hubDao()
    private val remote = HubRemoteDataSource(appContext)
    private val json = Json {
        ignoreUnknownKeys = true
        explicitNulls = false
    }
    private val assetCacheDirectory = File(appContext.cacheDir, "emucore-hub-assets").apply { mkdirs() }
    private val themeCatalogFile = File(appContext.filesDir, "hub-themes.json")
    private val _themes = MutableStateFlow(readThemeSnapshot())

    /** Last verified theme catalog, persisted so the Themes tab works offline. */
    val themes: StateFlow<HubThemeSnapshot?> = _themes.asStateFlow()

    private val _storeSync = MutableStateFlow<StoreSnapshot?>(null)

    /** Store catalog of the latest successful sync; StoreRepository persists and republishes it. */
    val storeSync: StateFlow<StoreSnapshot?> = _storeSync.asStateFlow()

    fun observeItems(languageTag: String?): Flow<List<HubItem>> {
        val locale = HubLocaleResolver.resolve(languageTag)
        return combine(dao.observeItems(locale), dao.observeFavorites()) { entities, favorites ->
            val favoriteIds = favorites.mapTo(HashSet()) { it.contentId }
            entities.map { entity -> entity.toDomain(entity.id in favoriteIds) }
        }
    }

    fun observeFavorites(): Flow<Set<String>> = dao.observeFavorites().combineWithIds()

    suspend fun sync(languageTag: String?, forceRefresh: Boolean = false): HubSyncResult = withContext(Dispatchers.IO) {
        val requestedLocale = HubLocaleResolver.resolve(languageTag)
        val previousState = dao.catalogState()
        val now = System.currentTimeMillis()
        if (!forceRefresh && previousState != null && previousState.locale == requestedLocale &&
            now - previousState.lastCheckedAtMillis < REFRESH_COOLDOWN_MS
        ) {
            return@withContext HubSyncResult(changed = false, usedLocale = previousState.locale)
        }

        val snapshot = remote.load(requestedLocale)
        require(snapshot.channel.catalogRevision >= (previousState?.catalogRevision ?: 0L)) {
            "Hub catalog rollback was rejected"
        }
        val assetEntities = snapshot.assets.map { asset -> asset.toEntity(snapshot.baseUrl, snapshot.manifest.catalogRevision) }
        val assetsById = assetEntities.associateBy(HubAssetEntity::assetId)
        val itemEntities = snapshot.items.map { item ->
            item.toEntity(
                locale = snapshot.locale,
                baseUrl = snapshot.baseUrl,
                revision = snapshot.manifest.catalogRevision,
                hero = item.heroAssetId?.let(assetsById::get)
            )
        }
        database.withTransaction {
            dao.upsertAssets(assetEntities)
            dao.upsertItems(itemEntities)
            dao.deleteStaleItems(snapshot.locale, snapshot.manifest.catalogRevision)
            dao.deleteStaleAssets(snapshot.manifest.catalogRevision)
            dao.upsertCatalogState(
                HubCatalogStateEntity(
                    releaseId = snapshot.manifest.releaseId,
                    catalogRevision = snapshot.manifest.catalogRevision,
                    locale = snapshot.locale,
                    commit = snapshot.channel.commit,
                    baseUrl = snapshot.baseUrl,
                    lastCheckedAtMillis = now,
                    updatedAtMillis = now
                )
            )
        }
        snapshot.store?.let { catalog ->
            _storeSync.value = StoreSnapshot(snapshot.baseUrl, snapshot.manifest.catalogRevision, catalog)
        }
        snapshot.themes?.let { catalog ->
            val themeSnapshot = HubThemeSnapshot(snapshot.baseUrl, snapshot.manifest.catalogRevision, catalog)
            writeThemeSnapshot(themeSnapshot)
            _themes.value = themeSnapshot
        }
        HubSyncResult(
            changed = previousState?.catalogRevision != snapshot.manifest.catalogRevision || previousState.locale != snapshot.locale,
            usedLocale = snapshot.locale,
            fallbackLocale = snapshot.locale.takeIf { it != requestedLocale }
        )
    }

    suspend fun loadArticle(contentId: String): HubArticle = withContext(Dispatchers.IO) {
        val entity = dao.item(contentId) ?: error("Hub content is unavailable")
        val item = entity.toDomain(dao.favorite(contentId) != null)
        val cached = dao.article(entity.id, entity.locale, entity.contentVersion)
        cached?.toArticle(item, fromCache = true)?.let { return@withContext it }

        val reference = HubFileReference(
            url = entity.documentUrl,
            sha256 = entity.documentSha256,
            bytes = entity.documentBytes,
            contentType = "application/json"
        )
        val raw = remote.fetchDocument(reference).toString(Charsets.UTF_8)
        val document = json.decodeFromString<HubLocalizedDocument>(raw)
        require(document.id == entity.id)
        require(document.locale == entity.locale)
        require(document.basedOnVersion == entity.contentVersion)
        require(document.status == "reviewed")
        database.withTransaction {
            dao.upsertArticle(
                HubArticleEntity(
                    contentId = entity.id,
                    locale = entity.locale,
                    contentVersion = entity.contentVersion,
                    rawJson = raw,
                    sha256 = entity.documentSha256,
                    cachedAtMillis = System.currentTimeMillis()
                )
            )
            dao.deleteOldArticleVersions(entity.id, entity.contentVersion)
        }
        HubArticle(
            item = item,
            document = document,
            assets = dao.assetsForContent(contentId).associateBy(HubAssetEntity::assetId),
            fromCache = false
        )
    }

    suspend fun relatedItems(contentId: String, limit: Int = 4): List<HubItem> = withContext(Dispatchers.IO) {
        if (limit <= 0) return@withContext emptyList()
        val sourceEntity = dao.item(contentId) ?: return@withContext emptyList()
        val source = sourceEntity.toDomain(dao.favorite(contentId) != null)
        val candidates = dao.items(sourceEntity.locale)
            .asSequence()
            .filter { it.id != contentId && it.kind == sourceEntity.kind }
            .map { entity ->
                val candidate = entity.toDomain(favorite = false)
                val sharedProducts = source.relatedProductIds.intersect(candidate.relatedProductIds.toSet()).size
                val sharedCategories = source.categories.intersect(candidate.categories.toSet()).size
                val sharedTags = source.tags.intersect(candidate.tags.toSet()).size
                val score = sharedProducts * 8 + sharedCategories * 4 + sharedTags
                candidate to score
            }
            .filter { (_, score) -> score >= RELATED_ITEM_MINIMUM_SCORE }
            .sortedWith(
                compareByDescending<Pair<HubItem, Int>> { it.second }
                    .thenByDescending { it.first.featured }
                    .thenByDescending { it.first.publishedAt }
            )
            .take(limit)
            .map { it.first }
            .toList()

        candidates.map { item -> item.copy(isFavorite = dao.favorite(item.id) != null) }
    }

    suspend fun toggleFavorite(item: HubItem): Boolean = withContext(Dispatchers.IO) {
        val existing = dao.favorite(item.id)
        if (existing != null) {
            dao.deleteFavorite(item.id)
            false
        } else {
            dao.upsertFavorite(
                HubFavoriteEntity(
                    contentId = item.id,
                    kind = item.kind.wireValue,
                    addedAtMillis = System.currentTimeMillis(),
                    lastKnownTitle = item.title,
                    lastKnownThumbnailUrl = item.heroThumbnailUrl
                )
            )
            true
        }
    }

    suspend fun setFavorite(contentId: String, favorite: Boolean): Boolean = withContext(Dispatchers.IO) {
        val entity = dao.item(contentId) ?: return@withContext false
        val item = entity.toDomain(dao.favorite(contentId) != null)
        val currentlyFavorite = dao.favorite(contentId) != null
        if (currentlyFavorite == favorite) return@withContext favorite
        toggleFavorite(item)
    }

    /** Downloads a theme package pinned by the catalog's commit and SHA-256. */
    suspend fun downloadThemePackage(entry: HubThemeEntry): ByteArray = withContext(Dispatchers.IO) {
        val snapshot = _themes.value ?: error("Hub themes are unavailable")
        remote.fetchThemePackage(snapshot.url(entry.packageFile), entry.packageFile)
    }

    /** Streams a pinned Store file into [target]; see [HubRemoteDataSource.downloadVerified]. */
    suspend fun downloadStoreFile(
        url: String,
        file: StoreFile,
        hardLimit: Long,
        target: File,
        onProgress: (Float) -> Unit
    ) = withContext(Dispatchers.IO) {
        val job = coroutineContext[kotlinx.coroutines.Job]
        remote.downloadVerified(url, file, hardLimit, target, { job?.isActive != false }, onProgress)
    }

    @kotlinx.serialization.Serializable
    private data class StoredThemeSnapshot(
        val baseUrl: String,
        val catalogRevision: Long,
        val catalog: HubThemeCatalog
    )

    private fun readThemeSnapshot(): HubThemeSnapshot? = runCatching {
        if (!themeCatalogFile.isFile) return null
        val stored = json.decodeFromString<StoredThemeSnapshot>(themeCatalogFile.readText())
        require(stored.baseUrl.startsWith("https://raw.githubusercontent.com/"))
        HubThemeSnapshot(stored.baseUrl, stored.catalogRevision, stored.catalog)
    }.getOrNull()

    private fun writeThemeSnapshot(snapshot: HubThemeSnapshot) {
        runCatching {
            val temporary = File(themeCatalogFile.parentFile, "${themeCatalogFile.name}.tmp")
            temporary.writeText(
                json.encodeToString(
                    StoredThemeSnapshot.serializer(),
                    StoredThemeSnapshot(snapshot.baseUrl, snapshot.catalogRevision, snapshot.catalog)
                )
            )
            if (!temporary.renameTo(themeCatalogFile)) {
                temporary.copyTo(themeCatalogFile, overwrite = true)
                temporary.delete()
            }
        }
    }

    suspend fun cachedAsset(
        url: String,
        sha256: String,
        expectedBytes: Long
    ): File = withContext(Dispatchers.IO) {
        require(url.startsWith("https://"))
        require(sha256.matches(Regex("[0-9a-f]{64}")))
        val target = File(assetCacheDirectory, sha256)
        if (target.isFile && target.length() == expectedBytes && target.sha256() == sha256) return@withContext target
        val temporary = File(assetCacheDirectory, "$sha256.part").apply { delete() }
        try {
            val bytes = remote.fetchAsset(url, sha256, expectedBytes)
            temporary.writeBytes(bytes)
            if (!temporary.renameTo(target)) {
                temporary.copyTo(target, overwrite = true)
                temporary.delete()
            }
            target
        } catch (error: Throwable) {
            temporary.delete()
            throw error
        }
    }

    private suspend fun HubArticleEntity.toArticle(item: HubItem, fromCache: Boolean): HubArticle? {
        val document = runCatching {
            json.decodeFromString<HubLocalizedDocument>(rawJson).also { parsed ->
                require(parsed.id == contentId && parsed.locale == locale && parsed.basedOnVersion == contentVersion)
            }
        }.getOrNull() ?: return null
        return HubArticle(
            item = item,
            document = document,
            assets = dao.assetsForContent(contentId).associateBy(HubAssetEntity::assetId),
            fromCache = fromCache
        )
    }

    private fun HubIndexItem.toEntity(
        locale: String,
        baseUrl: String,
        revision: Long,
        hero: HubAssetEntity?
    ): HubItemEntity {
        val kindValue = HubKind.fromWire(kind) ?: error("Unsupported Hub item kind")
        val documentPath = document.path ?: error("Hub document path is missing")
        return HubItemEntity(
            id = id,
            kind = kindValue.wireValue,
            contentVersion = contentVersion,
            localeVersion = localeVersion,
            locale = locale,
            title = title,
            summary = summary,
            publishedAt = publishedAt,
            updatedAt = updatedAt,
            eventDate = eventDate,
            datePrecision = datePrecision,
            year = year,
            categoriesJson = json.encodeToString(categoryIds),
            tagsJson = json.encodeToString(tagIds),
            relatedProductIdsJson = json.encodeToString(relatedProductIds),
            featured = featured,
            priority = priority,
            canonicalUrl = canonicalUrl,
            sourcesJson = json.encodeToString(sources),
            heroAssetId = heroAssetId,
            heroThumbnailUrl = thumbnailUrl ?: hero?.thumbnailUrl,
            heroThumbnailSha256 = hero?.thumbnailSha256,
            heroThumbnailBytes = hero?.thumbnailBytes,
            heroDisplayUrl = thumbnailUrl ?: hero?.displayUrl,
            heroDisplaySha256 = hero?.displaySha256,
            heroDisplayBytes = hero?.displayBytes,
            heroOriginalUrl = hero?.originalUrl,
            heroOriginalSha256 = hero?.originalSha256,
            heroOriginalBytes = hero?.originalBytes,
            imageDownloadAllowed = hero?.downloadAllowed == true,
            heroAttribution = hero?.attribution,
            heroSourceUrl = hero?.sourceUrl,
            heroLicenseId = hero?.licenseId,
            heroLicenseUrl = hero?.licenseUrl,
            documentUrl = resolve(baseUrl, documentPath),
            documentSha256 = document.sha256,
            documentBytes = document.bytes,
            providerId = providerId,
            channelTitle = channelTitle,
            sourceLanguage = sourceLanguage,
            thumbnailUrl = thumbnailUrl,
            catalogRevision = revision
        )
    }

    private fun HubAsset.toEntity(baseUrl: String, revision: Long): HubAssetEntity {
        val thumbnail = variants["thumbnail"]
        val display = variants["display"]
        val original = variants["original"]
        return HubAssetEntity(
            assetId = assetId,
            ownerContentId = ownerContentId,
            thumbnailUrl = thumbnail?.path?.let { resolve(baseUrl, it) },
            thumbnailSha256 = thumbnail?.sha256,
            thumbnailBytes = thumbnail?.bytes,
            displayUrl = display?.path?.let { resolve(baseUrl, it) },
            displaySha256 = display?.sha256,
            displayBytes = display?.bytes,
            originalUrl = original?.path?.let { resolve(baseUrl, it) },
            originalSha256 = original?.sha256,
            originalBytes = original?.bytes,
            creator = rights.creator,
            sourceUrl = rights.sourceUrl,
            licenseId = rights.licenseId,
            licenseUrl = rights.licenseUrl,
            attribution = rights.attribution,
            downloadAllowed = rights.downloadAllowed,
            catalogRevision = revision
        )
    }

    private fun HubItemEntity.toDomain(favorite: Boolean): HubItem = HubItem(
        id = id,
        kind = HubKind.fromWire(kind) ?: HubKind.NEWS,
        title = title,
        summary = summary,
        publishedAt = publishedAt,
        updatedAt = updatedAt,
        eventDate = eventDate,
        datePrecision = datePrecision,
        year = year,
        categories = json.decodeFromString(categoriesJson),
        tags = json.decodeFromString(tagsJson),
        relatedProductIds = json.decodeFromString(relatedProductIdsJson),
        featured = featured,
        priority = priority,
        canonicalUrl = canonicalUrl,
        sources = json.decodeFromString(sourcesJson),
        heroThumbnailUrl = heroThumbnailUrl,
        heroThumbnailSha256 = heroThumbnailSha256,
        heroThumbnailBytes = heroThumbnailBytes,
        heroDisplayUrl = heroDisplayUrl,
        heroDisplaySha256 = heroDisplaySha256,
        heroDisplayBytes = heroDisplayBytes,
        heroOriginalUrl = heroOriginalUrl,
        heroOriginalSha256 = heroOriginalSha256,
        heroOriginalBytes = heroOriginalBytes,
        imageDownloadAllowed = imageDownloadAllowed,
        heroAttribution = heroAttribution,
        heroSourceUrl = heroSourceUrl,
        heroLicenseId = heroLicenseId,
        heroLicenseUrl = heroLicenseUrl,
        providerId = providerId,
        channelTitle = channelTitle,
        sourceLanguage = sourceLanguage,
        isFavorite = favorite
    )

    private fun resolve(baseUrl: String, relativePath: String): String = URL(URL(baseUrl), relativePath).toString()

    companion object {
        private const val REFRESH_COOLDOWN_MS = 30L * 60L * 1000L
        private const val RELATED_ITEM_MINIMUM_SCORE = 4
        @Volatile private var instance: HubRepository? = null

        fun get(context: Context): HubRepository = instance ?: synchronized(this) {
            instance ?: HubRepository(context).also { instance = it }
        }
    }
}

private fun Flow<List<HubFavoriteEntity>>.combineWithIds(): Flow<Set<String>> = map { favorites ->
    favorites.mapTo(LinkedHashSet()) { it.contentId }
}

private fun File.sha256(): String {
    val digest = MessageDigest.getInstance("SHA-256")
    inputStream().buffered().use { input ->
        val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
        while (true) {
            val read = input.read(buffer)
            if (read < 0) break
            digest.update(buffer, 0, read)
        }
    }
    return digest.digest().joinToString("") { byte -> "%02x".format(byte.toInt() and 0xff) }
}
