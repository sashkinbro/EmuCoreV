package com.sbro.emucorev

import android.database.sqlite.SQLiteDatabase
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorev.core.vita.NativeLibraryCatalog
import com.sbro.emucorev.data.InstalledGameRepository
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

@RunWith(AndroidJUnit4::class)
class NativeLibraryCatalogDeviceTest {
    @Test fun updateDeleteAndReinstallKeepCatalogConsistent() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val original = File(context.cacheDir, "native-shell-runtime/vita/ur0/shell/db/app.db")
        val game = InstalledGameRepository().loadInstalledGames(context).firstOrNull()
        assumeTrue("Requires initialized firmware and an installed game", original.isFile && game != null)
        val sandbox = File(context.cacheDir, "native-shell-runtime/catalog-test-${System.nanoTime()}")
        val database = File(sandbox, "ur0/shell/db/app.db")
        database.parentFile!!.mkdirs()
        original.copyTo(database)
        fun scalar(sql: String, args: Array<String> = emptyArray()): String =
            SQLiteDatabase.openDatabase(database.path, null, SQLiteDatabase.OPEN_READONLY).use { db ->
                db.rawQuery(sql, args).use { cursor -> assertTrue(cursor.moveToFirst()); cursor.getString(0) }
            }
        try {
            assertEquals(1, NativeLibraryCatalog.synchronize(context, sandbox, listOf(game!!)))
            assertEquals("1", scalar("SELECT COUNT(*) FROM tbl_appinfo_icon WHERE titleId=?", arrayOf(game.titleId)))
            assertEquals("0", scalar("SELECT COUNT(*) FROM tbl_appinfo_icon WHERE titleId LIKE 'NPXS%' AND titleId!='NPXS10008'"))
            NativeLibraryCatalog.synchronize(context, sandbox, listOf(game.copy(title = "Updated title")))
            assertEquals("Updated title", scalar("SELECT title FROM tbl_appinfo_icon WHERE titleId=?", arrayOf(game.titleId)))
            NativeLibraryCatalog.synchronize(context, sandbox, emptyList())
            assertEquals("0", scalar("SELECT COUNT(*) FROM tbl_appinfo_icon WHERE titleId=?", arrayOf(game.titleId)))
            assertEquals("0", scalar("SELECT COUNT(*) FROM tbl_appinfo WHERE titleId=?", arrayOf(game.titleId)))
            assertFalse(File(sandbox, "ux0/app/${game.titleId}").exists())
            NativeLibraryCatalog.synchronize(context, sandbox, listOf(game))
            NativeLibraryCatalog.synchronize(context, sandbox, listOf(game))
            assertEquals("1", scalar("SELECT COUNT(*) FROM tbl_appinfo_icon WHERE titleId=?", arrayOf(game.titleId)))
            assertEquals("0", scalar("SELECT COUNT(*) FROM (SELECT pageId,pos,COUNT(*) n FROM tbl_appinfo_icon GROUP BY pageId,pos HAVING n>1)"))
            assertEquals("ok", scalar("PRAGMA integrity_check"))
        } finally {
            sandbox.deleteRecursively()
        }
    }
}
