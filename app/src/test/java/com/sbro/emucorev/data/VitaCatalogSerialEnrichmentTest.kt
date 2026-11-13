package com.sbro.emucorev.data

import android.content.Context
import android.content.ContextWrapper
import android.content.res.AssetManager
import android.database.sqlite.SQLiteDatabase
import java.io.File
import java.nio.file.Files
import java.util.zip.ZipOutputStream
import java.util.zip.ZipEntry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(manifest = Config.NONE, sdk = [29])
class VitaCatalogSerialEnrichmentTest {
    private val revision = "a".repeat(40)
    private fun index(): String = """{
      "schema_version":2,"asset_revision":"$revision",
      "games":{"42":{"path":"covers/42.webp","path_3d":"covers/3d/42.webp","sha256":"${"1".repeat(64)}","sha256_3d":"${"2".repeat(64)}","source_image_id":"coABC","source_url":"https://images.igdb.com/igdb/image/upload/t_original/coABC.jpg"}},
      "serials":{"PCSA00001":"42","PCSB00002":"42","PCSE00003":"42"}
    }"""

    private fun fixture(json: String = index(), block: (VitaCatalogRepository, File) -> Unit) {
        val dir = Files.createTempDirectory("catalog-serials-").toFile()
        try {
            val assets = File(dir, "assets").apply { mkdirs() }
            File(assets, "catalog/vita_covers.json").apply { parentFile.mkdirs(); writeText(json, Charsets.UTF_8) }
            val archive = File(dir, "fixture-assets.apk")
            ZipOutputStream(archive.outputStream()).use { zip ->
                zip.putNextEntry(ZipEntry("assets/catalog/vita_covers.json"))
                zip.write(json.toByteArray(Charsets.UTF_8)); zip.closeEntry()
            }
            val manager = AssetManager::class.java.getDeclaredConstructor().newInstance()
            AssetManager::class.java.getMethod("addAssetPath", String::class.java).invoke(manager, archive.path)
            val context = object : ContextWrapper(RuntimeEnvironment.getApplication()) {
                override fun getFilesDir(): File = dir
                override fun getAssets(): AssetManager {
                    check(!File(assets, "deny-further-asset-access").exists()) { "Index must already be cached" }
                    return manager
                }
            }
            SQLiteDatabase.openOrCreateDatabase(File(dir, "psvita_games.db"), null).use { db ->
                db.execSQL("CREATE TABLE games (igdb_id INTEGER PRIMARY KEY,name TEXT,normalized_name TEXT,year INTEGER,rating REAL,summary TEXT,cover_url TEXT,hero_url TEXT)")
                db.execSQL("CREATE TABLE game_serials (igdb_id INTEGER,serial TEXT)")
                db.execSQL("CREATE TABLE game_genres (igdb_id INTEGER,genre_name TEXT)")
                db.execSQL("CREATE TABLE game_screenshots (igdb_id INTEGER,image_url TEXT,position INTEGER)")
                db.execSQL("CREATE TABLE game_videos (igdb_id INTEGER,youtube_id TEXT,position INTEGER)")
                db.execSQL("INSERT INTO games VALUES(42,'Exact game','exact game',2012,80,'','','')")
                db.execSQL("INSERT INTO games VALUES(43,'Same title different IGDB','same title different igdb',2012,70,'','','')")
                db.execSQL("INSERT INTO game_serials VALUES(42,'PCSA00001')")
                db.execSQL("INSERT INTO game_serials VALUES(42,'PCSD00004')")
            }
            block(VitaCatalogRepository(context), assets)
            manager.close()
        } finally { dir.deleteRecursively() }
    }

    @Test fun detailsSearchAndEntriesUnionOnlyExactRowIdsWithDatabaseSerials() = fixture { repository, _ ->
        val expected = listOf("PCSA00001", "PCSD00004", "PCSB00002", "PCSE00003")
        assertEquals(expected, repository.getDetails(42)!!.serials)
        assertEquals(expected, repository.getEntries(listOf(42)).single().serials)
        assertEquals(expected, repository.search("").single { it.igdbId == 42L }.serials)
        assertTrue(repository.getDetails(43)!!.serials.isEmpty())
    }

    @Test fun indexIsLoadedOnceAndMalformedIndexKeepsExistingDatabaseSerials() {
        fixture { repository, assets ->
            val first = repository.getDetails(42)!!.serials
            File(assets, "deny-further-asset-access").writeText("deny", Charsets.UTF_8)
            assertEquals(first, repository.getDetails(42)!!.serials)
        }
        fixture("broken") { repository, _ ->
            assertEquals(listOf("PCSA00001", "PCSD00004"), repository.getDetails(42)!!.serials)
        }
    }
}
