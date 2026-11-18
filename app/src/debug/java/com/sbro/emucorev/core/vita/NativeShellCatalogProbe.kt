package com.sbro.emucorev.core.vita

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteDatabase
import com.sbro.emucorev.data.InstalledVitaGame
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Metadata bridge for the isolated firmware probe, never a game installation. */
object NativeShellCatalogProbe {
    private val utilities = setOf("NPXS10008") // Trophies only.

    fun synchronize(context: Context, sandbox: File, games: List<InstalledVitaGame>): Int {
        val privateRoot = File(context.cacheDir, "native-shell-runtime").canonicalFile
        require(sandbox.canonicalPath.startsWith(privateRoot.path + File.separator))
        val database = File(sandbox, "ur0/shell/db/app.db")
        check(database.isFile) { "The genuine shell must initialize its schema first" }
        val validGames = games.filter { it.titleId.matches(Regex("[A-Za-z0-9_]{1,64}")) }
        // Copy only shell presentation resources. Never expose executable,
        // license, save-data, or writable installed-game directories to VSH.
        validGames.forEach { game ->
            val source = File(game.installPath, "sce_sys").canonicalFile
            val target = File(sandbox, "ux0/app/${game.titleId}/sce_sys")
            listOf("param.sfo", "icon0.png", "pic0.png").forEach { name ->
                val file = File(source, name)
                if (file.isFile) copyChanged(file, File(target, name))
            }
            val livearea = File(source, "livearea")
            if (livearea.isDirectory) livearea.walkTopDown().filter(File::isFile).forEach { file ->
                require(file.canonicalPath.startsWith(source.path + File.separator))
                copyChanged(file, File(target, file.relativeTo(source).path))
            }
        }
        SQLiteDatabase.openDatabase(database.path, null, SQLiteDatabase.OPEN_READWRITE).use { db ->
            val allowed = utilities + validGames.map { it.titleId }
            db.beginTransaction()
            try {
                val obsolete = mutableListOf<String>()
                db.rawQuery("SELECT DISTINCT titleId FROM tbl_appinfo_icon", null).use { cursor ->
                    while (cursor.moveToNext()) {
                        val id = cursor.getString(0)
                        if (id !in allowed) obsolete += id
                    }
                }
                obsolete.forEach { db.delete("tbl_appinfo_icon", "titleId=?", arrayOf(it)) }
                validGames.forEach { game ->
                    updateMetadata(db, game)
                    db.rawQuery("SELECT 1 FROM tbl_appinfo_icon WHERE titleId=?", arrayOf(game.titleId)).use { cursor ->
                        if (!cursor.moveToFirst()) insertIcon(db, game)
                    }
                    val values = ContentValues().apply {
                        put("title", game.title)
                        put("iconPath", "ux0:app/${game.titleId}/sce_sys/icon0.png")
                    }
                    db.update("tbl_appinfo_icon", values, "titleId=?", arrayOf(game.titleId))
                }
                db.rawQuery("PRAGMA integrity_check", null).use { cursor ->
                    check(cursor.moveToFirst() && cursor.getString(0) == "ok")
                }
                db.setTransactionSuccessful()
            } finally {
                db.endTransaction()
            }
        }
        return validGames.size
    }

    private fun copyChanged(source: File, target: File) {
        if (target.isFile && target.length() == source.length() && target.lastModified() == source.lastModified()) return
        check(target.parentFile!!.isDirectory || target.parentFile!!.mkdirs())
        source.copyTo(target, overwrite = true)
        target.setLastModified(source.lastModified())
    }

    private fun insertIcon(db: SQLiteDatabase, game: InstalledVitaGame) {
        val pages = mutableListOf<Int>()
        db.rawQuery("SELECT pageId FROM tbl_appinfo_page WHERE pageNo>=0 ORDER BY pageNo", null).use { c ->
            while (c.moveToNext()) pages += c.getInt(0)
        }
        for (page in pages) {
            val occupied = mutableSetOf<Int>()
            // Firmware declares pageId without a type in this table. Bind an
            // integer explicitly: a TEXT parameter does not match its integer.
            db.rawQuery("SELECT pos FROM tbl_appinfo_icon WHERE pageId=CAST(? AS INTEGER)", arrayOf(page.toString())).use { c ->
                while (c.moveToNext()) occupied += c.getInt(0)
            }
            val slot = (0..9).firstOrNull { it !in occupied } ?: continue
            writeIcon(db, page, slot, game)
            return
        }
        val page = db.rawQuery("SELECT COALESCE(MAX(pageId),0)+1 FROM tbl_appinfo_page", null).use { c -> c.moveToFirst(); c.getInt(0) }
        val number = db.rawQuery("SELECT COALESCE(MAX(pageNo),-1)+1 FROM tbl_appinfo_page WHERE pageNo>=0", null).use { c -> c.moveToFirst(); c.getInt(0) }
        db.insertOrThrow("tbl_appinfo_page", null, ContentValues().apply {
            put("pageId", page); put("pageNo", number); put("bgColor", 0)
            put("texWidth", 0); put("texHeight", 0); put("imageWidth", 0); put("imageHeight", 0)
            put("reserved01", 33554431)
        })
        writeIcon(db, page, 0, game)
    }

    private fun writeIcon(db: SQLiteDatabase, page: Int, slot: Int, game: InstalledVitaGame) {
        db.insertOrThrow("tbl_appinfo_icon", null, ContentValues().apply {
            put("pageId", page); put("pos", slot); put("titleId", game.titleId)
            put("iconPath", "ux0:app/${game.titleId}/sce_sys/icon0.png")
            put("title", game.title); put("type", 0); put("icon0Type", 5)
            put("parentalLockLv", 0); put("status", 0)
        })
    }

    private fun updateMetadata(db: SQLiteDatabase, game: InstalledVitaGame) {
        val sfo = readSfo(File(game.installPath, "sce_sys/param.sfo"))
        val root = "ux0:app/${game.titleId}"
        // Native SceLsdb indexes SFO fields by FNV-1a, including BOOTABLE,
        // PARENTAL_LEVEL and localized titles. Preserve the original types:
        // a sparse hand-picked subset makes LiveArea report a corrupt DB.
        val fields = linkedMapOf<Long, Any>(
            0x10954174L to game.titleId, 0x35ef415bL to game.titleId,
            0xf11f5930L to game.titleId, 0xf960d54fL to game.titleId,
            0x21ca76b1L to (game.category ?: "gd"),
            0x222641e9L to game.title, 0xeed70ec4L to (sfo["STITLE"] ?: game.title),
            0x9a9b3542L to root, 0xb4232566L to "$root/eboot.bin",
            0x9ccbede2L to "$root/sce_sys/livearea/contents", 0x6711f0adL to "$root/sce_sys/pic0.png",
            0xbcd7161eL to (game.version ?: "01.00"), 0xd3bbb9b7L to (sfo["VERSION"] ?: "01.00")
        )
        sfo.forEach { (name, value) -> fields[sfoKey(name)] = value }
        // Host-installed entries are ordinary local applications. In the first
        // probe this internal key was mistaken for PARENTAL_LEVEL; explicitly
        // replace those stale flags as well, rather than preserving fake PSN
        // requirements from an earlier catalog synchronization.
        fields[sfoKey("#_promote_attr")] = 0L
        fields[sfoKey("#appconf")] = 0L
        // VSH's gd entry validation requires this numeric inventory field
        // (0x81029092 in the inspected firmware). Its genuine default is zero;
        // omitting it returns 0x80101107 before LiveArea can open.
        fields.putIfAbsent(0xe2327e05L, 0L)
        fields.forEach { (key, value) ->
            db.insertWithOnConflict("tbl_appinfo", null, ContentValues().apply {
                put("titleId", game.titleId); put("key", key)
                when (value) {
                    is Long -> put("val", value)
                    else -> put("val", value.toString())
                }
            }, SQLiteDatabase.CONFLICT_REPLACE)
        }
    }

    private fun sfoKey(name: String): Long = name.encodeToByteArray().fold(0x811c9dc5L) { hash, byte ->
        ((hash xor (byte.toLong() and 0xff)) * 0x01000193L) and 0xffffffffL
    }

    private fun readSfo(file: File): Map<String, Any> {
        val bytes = file.readBytes()
        require(bytes.size >= 20)
        val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        require(b.getInt(0) == 0x46535000)
        val keys = b.getInt(8); val values = b.getInt(12); val count = b.getInt(16)
        require(keys in 20..bytes.size && values in keys..bytes.size && count in 0..((bytes.size - 20) / 16))
        return buildMap {
            repeat(count) { index ->
                val entry = 20 + index * 16
                val key = keys.toLong() + (b.getShort(entry).toInt() and 0xffff)
                val start = values.toLong() + b.getInt(entry + 12)
                val length = b.getInt(entry + 4)
                require(key in keys.toLong() until values.toLong() && start >= values && length >= 0 && start + length <= bytes.size)
                var end = key.toInt()
                while (end < values && bytes[end] != 0.toByte()) end++
                require(end < values)
                val name = bytes.copyOfRange(key.toInt(), end).decodeToString()
                when (b.getShort(entry + 2).toInt() and 0xffff) {
                    0x404 -> { require(length >= 4); put(name, b.getInt(start.toInt()).toLong() and 0xffffffffL) }
                    0x204 -> put(name, bytes.copyOfRange(start.toInt(), (start + length).toInt()).decodeToString().trimEnd('\u0000'))
                }
            }
        }
    }
}
