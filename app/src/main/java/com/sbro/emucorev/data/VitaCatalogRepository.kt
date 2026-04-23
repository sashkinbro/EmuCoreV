package com.sbro.emucorev.data

import android.content.Context
import android.database.sqlite.SQLiteDatabase
import java.io.File

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
            val like = "%${trimmed.lowercase()}%"
            args += like
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

    fun findBestMatch(gameName: String): VitaCatalogEntry? {
        val query = gameName.trim()
        if (query.isBlank()) return null
        return search(query = query, limit = 25).firstOrNull {
            it.name.equals(query, ignoreCase = true) || it.name.contains(query, ignoreCase = true)
        } ?: search(query = query, limit = 1).firstOrNull()
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
