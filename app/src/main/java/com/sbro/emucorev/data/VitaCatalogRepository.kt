package com.sbro.emucorev.data

import android.content.Context
import android.database.sqlite.SQLiteDatabase
import java.io.File
import java.util.Locale

class VitaCatalogRepository(private val context: Context) {
    private val assetName = "catalog/psvita_games.db"
    private val localDbName = "psvita_games.db"

    fun hasCatalog(): Boolean = getCatalogCount() > 0

    fun getCatalogCount(): Int = querySingleInt("SELECT COUNT(*) FROM games")

    fun getAvailableGenres(): List<String> {
        return openDatabase()?.use { database ->
            database.rawQuery(
                """
                SELECT DISTINCT genre_name
                FROM game_genres
                WHERE genre_name IS NOT NULL AND TRIM(genre_name) <> ''
                ORDER BY genre_name COLLATE NOCASE ASC
                """.trimIndent(),
                emptyArray()
            ).use { cursor ->
                buildList {
                    while (cursor.moveToNext()) {
                        cursor.getString(0)?.takeIf(String::isNotBlank)?.let(::add)
                    }
                }
            }
        }.orEmpty()
    }

    fun getAvailableYears(): List<Int> {
        return openDatabase()?.use { database ->
            database.rawQuery(
                """
                SELECT DISTINCT year
                FROM games
                WHERE year IS NOT NULL
                ORDER BY year DESC
                """.trimIndent(),
                emptyArray()
            ).use { cursor ->
                buildList {
                    while (cursor.moveToNext()) {
                        if (!cursor.isNull(0)) add(cursor.getInt(0))
                    }
                }
            }
        }.orEmpty()
    }

    fun search(
        query: String,
        genre: String? = null,
        year: Int? = null,
        minRating: Float? = null,
        limit: Int = 80,
        offset: Int = 0
    ): List<VitaCatalogEntry> {
        val trimmed = query.trim()
        val conditions = mutableListOf<String>()
        val args = mutableListOf<String>()

        if (trimmed.isNotBlank()) {
            conditions += "(g.normalized_name LIKE ? OR g.name LIKE ?)"
            val normalizedQuery = normalizePunctuation(trimmed).ifBlank { trimmed.lowercase(Locale.ROOT) }
            args += "%$normalizedQuery%"
            args += "%$trimmed%"
        }
        if (!genre.isNullOrBlank()) {
            conditions += "EXISTS (SELECT 1 FROM game_genres gg WHERE gg.igdb_id = g.igdb_id AND gg.genre_name = ?)"
            args += genre
        }
        if (year != null) {
            conditions += "g.year = ?"
            args += year.toString()
        }
        if (minRating != null) {
            conditions += "g.rating >= ?"
            args += minRating.toString()
        }

        val whereClause = if (conditions.isEmpty()) "" else "WHERE ${conditions.joinToString(" AND ")}"
        val sql = """
            SELECT g.igdb_id, g.name, g.year, g.rating, g.summary, g.cover_url, g.hero_url
            FROM games g
            $whereClause
            ORDER BY g.rating DESC, g.name COLLATE NOCASE ASC
            LIMIT ? OFFSET ?
        """.trimIndent()
        args += limit.toString()
        args += offset.toString()

        return openDatabase()?.use { database ->
            database.rawQuery(sql, args.toTypedArray()).use { cursor ->
                buildList {
                    while (cursor.moveToNext()) {
                        val igdbId = cursor.getLong(0)
                        add(
                            VitaCatalogEntry(
                                igdbId = igdbId,
                                name = cursor.getString(1).orEmpty(),
                                year = cursor.takeIf { !it.isNull(2) }?.getInt(2),
                                rating = cursor.takeIf { !it.isNull(3) }?.getFloat(3),
                                summary = cursor.getString(4),
                                coverUrl = cursor.getString(5),
                                heroUrl = cursor.getString(6),
                                genres = loadGenres(database, igdbId),
                                serials = loadSerials(database, igdbId)
                            )
                        )
                    }
                }
            }
        }.orEmpty()
    }

    fun findBySerial(serial: String): VitaCatalogEntry? {
        val value = serial.trim()
        if (value.isBlank()) return null
        return openDatabase()?.use { database ->
            database.rawQuery(
                """
                SELECT g.igdb_id, g.name, g.year, g.rating, g.summary, g.cover_url, g.hero_url
                FROM games g
                INNER JOIN game_serials s ON s.igdb_id = g.igdb_id
                WHERE s.serial = ? COLLATE NOCASE
                LIMIT 1
                """.trimIndent(),
                arrayOf(value)
            ).use { cursor ->
                if (!cursor.moveToFirst()) return@use null
                val igdbId = cursor.getLong(0)
                VitaCatalogEntry(
                    igdbId = igdbId,
                    name = cursor.getString(1).orEmpty(),
                    year = cursor.takeIf { !it.isNull(2) }?.getInt(2),
                    rating = cursor.takeIf { !it.isNull(3) }?.getFloat(3),
                    summary = cursor.getString(4),
                    coverUrl = cursor.getString(5),
                    heroUrl = cursor.getString(6),
                    genres = loadGenres(database, igdbId),
                    serials = loadSerials(database, igdbId)
                )
            }
        }
    }

    fun findBestMatch(gameName: String): VitaCatalogEntry? {
        val query = gameName.trim()
        if (query.isBlank()) return null
        val queryTokens = tokens(query)
        if (queryTokens.isNotEmpty()) {
            val candidates = findByNormalizedToken(queryTokens.first())
            val scored = candidates.map { candidate ->
                val nameTokens = tokens(candidate.name)
                val common = queryTokens.count { it in nameTokens }
                val score = if (common == 0) 0f else {
                    common.toFloat() / maxOf(queryTokens.size, nameTokens.size)
                }
                candidate to score
            }.filter { it.second >= 0.5f }
            scored.maxByOrNull { it.second }?.let { return it.first }
        }
        return search(query = query, limit = 25).firstOrNull {
            it.name.equals(query, ignoreCase = true) || it.name.contains(query, ignoreCase = true)
        } ?: search(query = query, limit = 1).firstOrNull()
    }

    /** Ignores punctuation and case, matching how catalog names are normalized. */
    private fun normalizePunctuation(value: String): String {
        return value.lowercase(Locale.ROOT)
            .replace(Regex("[^a-z0-9]+"), " ")
            .trim()
            .replace(Regex("\\s+"), " ")
    }

    /** Significant words only: single letters ("s" from "Assassin's") are dropped. */
    private fun tokens(value: String): List<String> {
        return normalizePunctuation(value)
            .split(' ')
            .filter { it.isNotBlank() && (it.length > 1 || it[0].isDigit()) }
    }

    private fun findByNormalizedToken(token: String): List<VitaCatalogEntry> {
        if (token.isBlank()) return emptyList()
        return openDatabase()?.use { database ->
            database.rawQuery(
                """
                SELECT igdb_id, name, year, rating, summary, cover_url, hero_url
                FROM games
                WHERE normalized_name LIKE ?
                ORDER BY rating DESC, name COLLATE NOCASE ASC
                LIMIT 80
                """.trimIndent(),
                arrayOf("%$token%")
            ).use { cursor ->
                buildList {
                    while (cursor.moveToNext()) {
                        val igdbId = cursor.getLong(0)
                        add(
                            VitaCatalogEntry(
                                igdbId = igdbId,
                                name = cursor.getString(1).orEmpty(),
                                year = cursor.takeIf { !it.isNull(2) }?.getInt(2),
                                rating = cursor.takeIf { !it.isNull(3) }?.getFloat(3),
                                summary = cursor.getString(4),
                                coverUrl = cursor.getString(5),
                                heroUrl = cursor.getString(6),
                                genres = loadGenres(database, igdbId),
                                serials = loadSerials(database, igdbId)
                            )
                        )
                    }
                }
            }
        }.orEmpty()
    }

    fun getDetails(igdbId: Long): VitaCatalogDetails? {
        return openDatabase()?.use { database ->
            database.rawQuery(
                """
                SELECT igdb_id, name, year, rating, summary, cover_url, hero_url
                FROM games
                WHERE igdb_id = ?
                LIMIT 1
                """.trimIndent(),
                arrayOf(igdbId.toString())
            ).use { cursor ->
                if (!cursor.moveToFirst()) return@use null
                VitaCatalogDetails(
                    igdbId = cursor.getLong(0),
                    name = cursor.getString(1).orEmpty(),
                    year = cursor.takeIf { !it.isNull(2) }?.getInt(2),
                    rating = cursor.takeIf { !it.isNull(3) }?.getFloat(3),
                    summary = cursor.getString(4),
                    coverUrl = cursor.getString(5),
                    heroUrl = cursor.getString(6),
                    genres = loadGenres(database, igdbId),
                    serials = loadSerials(database, igdbId),
                    screenshots = loadScreenshots(database, igdbId),
                    videos = loadVideos(database, igdbId)
                )
            }
        }
    }

    fun getEntries(igdbIds: Collection<Long>): List<VitaCatalogEntry> {
        val ids = igdbIds.distinct()
        if (ids.isEmpty()) return emptyList()
        return openDatabase()?.use { database ->
            ids.mapNotNull { igdbId ->
                database.rawQuery(
                    """
                    SELECT igdb_id, name, year, rating, summary, cover_url, hero_url
                    FROM games
                    WHERE igdb_id = ?
                    LIMIT 1
                    """.trimIndent(),
                    arrayOf(igdbId.toString())
                ).use { cursor ->
                    if (!cursor.moveToFirst()) return@mapNotNull null
                    VitaCatalogEntry(
                        igdbId = cursor.getLong(0),
                        name = cursor.getString(1).orEmpty(),
                        year = cursor.takeIf { !it.isNull(2) }?.getInt(2),
                        rating = cursor.takeIf { !it.isNull(3) }?.getFloat(3),
                        summary = cursor.getString(4),
                        coverUrl = cursor.getString(5),
                        heroUrl = cursor.getString(6),
                        genres = loadGenres(database, igdbId),
                        serials = loadSerials(database, igdbId)
                    )
                }
            }
        }.orEmpty()
    }

    fun findBestMatchDetails(gameName: String): VitaCatalogDetails? {
        val match = findBestMatch(gameName) ?: return null
        return getDetails(match.igdbId)
    }

    private fun querySingleInt(sql: String): Int {
        return openDatabase()?.use { database ->
            database.rawQuery(sql, emptyArray()).use { cursor ->
                if (cursor.moveToFirst()) cursor.getInt(0) else 0
            }
        } ?: 0
    }

    private fun loadGenres(database: SQLiteDatabase, igdbId: Long): List<String> {
        return database.rawQuery(
            """
            SELECT genre_name
            FROM game_genres
            WHERE igdb_id = ?
            ORDER BY genre_name COLLATE NOCASE ASC
            """.trimIndent(),
            arrayOf(igdbId.toString())
        ).use { cursor ->
            buildList {
                while (cursor.moveToNext()) {
                    cursor.getString(0)?.takeIf(String::isNotBlank)?.let(::add)
                }
            }
        }
    }

    private fun loadScreenshots(database: SQLiteDatabase, igdbId: Long): List<String> {
        return database.rawQuery(
            """
            SELECT image_url
            FROM game_screenshots
            WHERE igdb_id = ?
            ORDER BY position ASC
            LIMIT 10
            """.trimIndent(),
            arrayOf(igdbId.toString())
        ).use { cursor ->
            buildList {
                while (cursor.moveToNext()) {
                    cursor.getString(0)?.let(::add)
                }
            }
        }
    }

    private fun loadSerials(database: SQLiteDatabase, igdbId: Long): List<String> {
        return database.rawQuery(
            """
            SELECT serial
            FROM game_serials
            WHERE igdb_id = ?
            ORDER BY serial COLLATE NOCASE ASC
            """.trimIndent(),
            arrayOf(igdbId.toString())
        ).use { cursor ->
            buildList {
                while (cursor.moveToNext()) {
                    cursor.getString(0)?.trim()?.takeIf(String::isNotBlank)?.let(::add)
                }
            }
        }
    }

    private fun loadVideos(database: SQLiteDatabase, igdbId: Long): List<String> {
        return database.rawQuery(
            """
            SELECT youtube_id
            FROM game_videos
            WHERE igdb_id = ?
            ORDER BY position ASC
            LIMIT 10
            """.trimIndent(),
            arrayOf(igdbId.toString())
        ).use { cursor ->
            buildList {
                while (cursor.moveToNext()) {
                    cursor.getString(0)?.let(::add)
                }
            }
        }
    }

    private fun openDatabase(): SQLiteDatabase? {
        val dbFile = prepareLocalDatabase() ?: return null
        return runCatching {
            SQLiteDatabase.openDatabase(dbFile.absolutePath, null, SQLiteDatabase.OPEN_READONLY)
        }.getOrNull()
    }

    private fun prepareLocalDatabase(): File? {
        val target = File(context.filesDir, localDbName)
        if (target.exists() && target.length() > 0) return target
        return runCatching {
            context.assets.open(assetName).use { input ->
                target.outputStream().use { output -> input.copyTo(output) }
            }
            target
        }.getOrNull()
    }
}
