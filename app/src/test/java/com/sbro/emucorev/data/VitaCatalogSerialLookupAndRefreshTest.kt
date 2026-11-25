package com.sbro.emucorev.data

import android.content.Context
import android.content.ContextWrapper
import android.content.res.AssetManager
import android.database.sqlite.SQLiteDatabase
import java.io.File
import java.nio.file.Files
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

/** B100 (exact serial lookup against an empty game_serials table) and B101 (atomic catalog refresh). */
@RunWith(RobolectricTestRunner::class)
@Config(manifest = Config.NONE, sdk = [29])
class VitaCatalogSerialLookupAndRefreshTest {
    private val revision = "a".repeat(40)
    private val coversJson = """{
      "schema_version":2,"asset_revision":"$revision",
      "games":{"42":{"path":"covers/42.webp","path_3d":"covers/3d/42.webp","sha256":"${"1".repeat(64)}","sha256_3d":"${"2".repeat(64)}","source_image_id":"coABC","source_url":"https://images.igdb.com/igdb/image/upload/t_original/coABC.jpg"}},
      "serials":{"PCSA00001":"42"}
    }"""

    private fun buildDb(dir: File, name: String, gameName: String): ByteArray {
        val file = File(dir, name)
        SQLiteDatabase.openOrCreateDatabase(file, null).use { db ->
            db.execSQL("CREATE TABLE games (igdb_id INTEGER PRIMARY KEY,name TEXT,normalized_name TEXT,year INTEGER,rating REAL,summary TEXT,cover_url TEXT,hero_url TEXT)")
            db.execSQL("CREATE TABLE game_serials (igdb_id INTEGER,serial TEXT)") // intentionally empty, like the bundled DB
            db.execSQL("CREATE TABLE game_genres (igdb_id INTEGER,genre_name TEXT)")
            db.execSQL("CREATE TABLE game_screenshots (igdb_id INTEGER,image_url TEXT,position INTEGER)")
            db.execSQL("CREATE TABLE game_videos (igdb_id INTEGER,youtube_id TEXT,position INTEGER)")
            db.execSQL("INSERT INTO games VALUES(42,'$gameName','${gameName.lowercase()}',2012,80,'','https://x/42.jpg','')")
            db.execSQL("INSERT INTO games VALUES(43,'$gameName 2','${gameName.lowercase()} 2',2013,70,'','https://x/43.jpg','')")
        }
        return file.readBytes().also { file.delete() }
    }

    private fun contextFor(dir: File, assetDb: ByteArray): Context {
        val archive = File(dir, "fixture-assets-${System.nanoTime()}.apk")
        ZipOutputStream(archive.outputStream()).use { zip ->
            zip.putNextEntry(ZipEntry("assets/catalog/vita_covers.json")); zip.write(coversJson.toByteArray()); zip.closeEntry()
            zip.putNextEntry(ZipEntry("assets/catalog/psvita_games.db")); zip.write(assetDb); zip.closeEntry()
        }
        val manager = AssetManager::class.java.getDeclaredConstructor().newInstance()
        AssetManager::class.java.getMethod("addAssetPath", String::class.java).invoke(manager, archive.path)
        return object : ContextWrapper(RuntimeEnvironment.getApplication()) {
            override fun getFilesDir(): File = dir
            override fun getAssets(): AssetManager = manager
        }
    }

    private fun temp() = Files.createTempDirectory("catalog-refresh-").toFile()

    @Test fun findBySerialUsesReviewedIndexWhenDbSerialTableIsEmptyAndNeverGuessesByName() {
        val dir = temp()
        try {
            val repo = VitaCatalogRepository(contextFor(dir, buildDb(dir, "src.db", "Exact game")))
            val hit = repo.findBySerial("pcsa-00001")
            assertEquals(42L, hit!!.igdbId)
            assertEquals("Exact game", hit.name)
            assertNull(repo.findBySerial("PCSA99999"))
            // A well-formed but unknown serial must not fall back to a title match (sequel 43 shares the name prefix).
            assertNull(repo.findForGame("PCSA99999", "Exact game 2"))
            assertEquals(42L, repo.findForGame("PCSA00001", "Exact game 2")!!.igdbId)
            // Only without any usable serial is the name fallback allowed.
            assertNotNull(repo.findForGame(null, "Exact game"))
            assertNotNull(repo.findForGame("homebrew", "Exact game"))
        } finally { dir.deleteRecursively() }
    }

    @Test fun staleLocalCopyIsReplacedWhenTheBundledAssetChanges() {
        val dir = temp()
        try {
            val old = VitaCatalogRepository(contextFor(dir, buildDb(dir, "o.db", "Old name")))
            assertEquals("Old name", old.findBySerial("PCSA00001")!!.name)
            // Same files dir (app update), new bundled DB.
            val updated = VitaCatalogRepository(contextFor(dir, buildDb(dir, "n.db", "New name")))
            assertEquals("New name", updated.findBySerial("PCSA00001")!!.name)
            assertFalse(File(dir, "psvita_games.db.tmp").exists())
        } finally { dir.deleteRecursively() }
    }

    @Test fun truncatedLocalCopyIsRepaired() {
        val dir = temp()
        try {
            val bytes = buildDb(dir, "t.db", "Fresh")
            VitaCatalogRepository(contextFor(dir, bytes)).findBySerial("PCSA00001")
            val local = File(dir, "psvita_games.db")
            local.writeBytes(local.readBytes().copyOf(1500)) // killed mid-copy, marker still present
            File(dir, "psvita_games.db.sha256").delete()
            assertEquals("Fresh", VitaCatalogRepository(contextFor(dir, bytes)).findBySerial("PCSA00001")!!.name)
            // Marker intact but file corrupt: integrity check still repairs it.
            local.writeBytes(ByteArray(4096) { 7 })
            assertEquals("Fresh", VitaCatalogRepository(contextFor(dir, bytes)).findBySerial("PCSA00001")!!.name)
        } finally { dir.deleteRecursively() }
    }
}
