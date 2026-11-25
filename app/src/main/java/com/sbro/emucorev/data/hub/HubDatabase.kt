package com.sbro.emucorev.data.hub

import android.content.Context
import androidx.room.Dao
import androidx.room.Database
import androidx.room.AutoMigration
import androidx.room.Entity
import androidx.room.Index
import androidx.room.Insert
import androidx.room.OnConflictStrategy
import androidx.room.PrimaryKey
import androidx.room.Query
import androidx.room.Room
import androidx.room.RoomDatabase
import kotlinx.coroutines.flow.Flow

@Entity(
    tableName = "hub_items",
    indices = [Index("locale"), Index("kind"), Index("publishedAt"), Index("eventDate")]
)
data class HubItemEntity(
    @PrimaryKey val id: String,
    val kind: String,
    val contentVersion: Int,
    val localeVersion: Int,
    val locale: String,
    val title: String,
    val summary: String,
    val publishedAt: String,
    val updatedAt: String,
    val eventDate: String?,
    val datePrecision: String?,
    val year: Int?,
    val categoriesJson: String,
    val tagsJson: String,
    val relatedProductIdsJson: String,
    val featured: Boolean,
    val priority: Int,
    val canonicalUrl: String,
    val sourcesJson: String,
    val heroAssetId: String?,
    val heroThumbnailUrl: String?,
    val heroThumbnailSha256: String?,
    val heroThumbnailBytes: Long?,
    val heroDisplayUrl: String?,
    val heroDisplaySha256: String?,
    val heroDisplayBytes: Long?,
    val heroOriginalUrl: String?,
    val heroOriginalSha256: String?,
    val heroOriginalBytes: Long?,
    val imageDownloadAllowed: Boolean,
    val heroAttribution: String?,
    val heroSourceUrl: String?,
    val heroLicenseId: String?,
    val heroLicenseUrl: String?,
    val documentUrl: String,
    val documentSha256: String,
    val documentBytes: Long,
    val providerId: String?,
    val channelTitle: String?,
    val sourceLanguage: String?,
    val thumbnailUrl: String?,
    val catalogRevision: Long
)

@Entity(tableName = "hub_assets")
data class HubAssetEntity(
    @PrimaryKey val assetId: String,
    val ownerContentId: String,
    val thumbnailUrl: String?,
    val thumbnailSha256: String?,
    val thumbnailBytes: Long?,
    val displayUrl: String?,
    val displaySha256: String?,
    val displayBytes: Long?,
    val originalUrl: String?,
    val originalSha256: String?,
    val originalBytes: Long?,
    val creator: String,
    val sourceUrl: String,
    val licenseId: String,
    val licenseUrl: String?,
    val attribution: String?,
    val downloadAllowed: Boolean,
    val catalogRevision: Long
)

@Entity(tableName = "hub_articles", primaryKeys = ["contentId", "locale", "contentVersion"])
data class HubArticleEntity(
    val contentId: String,
    val locale: String,
    val contentVersion: Int,
    val rawJson: String,
    val sha256: String,
    val cachedAtMillis: Long
)

@Entity(tableName = "hub_favorites")
data class HubFavoriteEntity(
    @PrimaryKey val contentId: String,
    val kind: String,
    val addedAtMillis: Long,
    val lastKnownTitle: String,
    val lastKnownThumbnailUrl: String?
)

@Entity(tableName = "hub_catalog_state")
data class HubCatalogStateEntity(
    @PrimaryKey val id: Int = 1,
    val releaseId: String,
    val catalogRevision: Long,
    val locale: String,
    val commit: String,
    val baseUrl: String,
    val lastCheckedAtMillis: Long,
    val updatedAtMillis: Long
)

@Dao
interface HubDao {
    @Query(
        """
        SELECT * FROM hub_items
        WHERE locale = :locale
        ORDER BY
            CASE
                WHEN kind = 'history' AND eventDate IS NOT NULL AND eventDate != '' THEN eventDate
                ELSE publishedAt
            END DESC,
            updatedAt DESC,
            id ASC
        """
    )
    fun observeItems(locale: String): Flow<List<HubItemEntity>>

    @Query("SELECT * FROM hub_items WHERE locale = :locale")
    suspend fun items(locale: String): List<HubItemEntity>

    @Query("SELECT * FROM hub_items WHERE id = :id LIMIT 1")
    suspend fun item(id: String): HubItemEntity?

    @Query("SELECT * FROM hub_items WHERE id = :id LIMIT 1")
    fun observeItem(id: String): Flow<HubItemEntity?>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun upsertItems(items: List<HubItemEntity>)

    @Query("DELETE FROM hub_items WHERE locale = :locale AND catalogRevision != :catalogRevision")
    suspend fun deleteStaleItems(locale: String, catalogRevision: Long)

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun upsertAssets(assets: List<HubAssetEntity>)

    @Query("DELETE FROM hub_assets WHERE catalogRevision != :catalogRevision")
    suspend fun deleteStaleAssets(catalogRevision: Long)

    @Query("SELECT * FROM hub_assets WHERE assetId = :assetId LIMIT 1")
    suspend fun asset(assetId: String): HubAssetEntity?

    @Query("SELECT * FROM hub_assets WHERE ownerContentId = :contentId")
    suspend fun assetsForContent(contentId: String): List<HubAssetEntity>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun upsertArticle(article: HubArticleEntity)

    @Query("SELECT * FROM hub_articles WHERE contentId = :contentId AND locale = :locale AND contentVersion = :version LIMIT 1")
    suspend fun article(contentId: String, locale: String, version: Int): HubArticleEntity?

    @Query("DELETE FROM hub_articles WHERE contentId = :contentId AND contentVersion != :version")
    suspend fun deleteOldArticleVersions(contentId: String, version: Int)

    @Query("SELECT * FROM hub_favorites ORDER BY addedAtMillis DESC")
    fun observeFavorites(): Flow<List<HubFavoriteEntity>>

    @Query("SELECT * FROM hub_favorites WHERE contentId = :contentId LIMIT 1")
    suspend fun favorite(contentId: String): HubFavoriteEntity?

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun upsertFavorite(favorite: HubFavoriteEntity)

    @Query("DELETE FROM hub_favorites WHERE contentId = :contentId")
    suspend fun deleteFavorite(contentId: String)

    @Query("SELECT * FROM hub_catalog_state WHERE id = 1")
    suspend fun catalogState(): HubCatalogStateEntity?

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun upsertCatalogState(state: HubCatalogStateEntity)
}

@Database(
    entities = [
        HubItemEntity::class,
        HubAssetEntity::class,
        HubArticleEntity::class,
        HubFavoriteEntity::class,
        HubCatalogStateEntity::class
    ],
    version = 2,
    autoMigrations = [AutoMigration(from = 1, to = 2)],
    exportSchema = true
)
abstract class HubDatabase : RoomDatabase() {
    abstract fun hubDao(): HubDao

    companion object {
        @Volatile private var instance: HubDatabase? = null

        fun get(context: Context): HubDatabase = instance ?: synchronized(this) {
            instance ?: Room.databaseBuilder(
                context.applicationContext,
                HubDatabase::class.java,
                "emucore-hub.db"
            ).build().also { instance = it }
        }
    }
}
