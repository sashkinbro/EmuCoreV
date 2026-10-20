package com.sbro.emucorev.data

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

/** One compressed account file containing the native progress consumed by AchievementsScreen. */
internal object TrophyProgressArchive {
    const val PROGRESS_NAME = "TROPUSR.DAT"
    const val PROGRESS_BYTES = 1648
    private const val MAX_GAMES = 4096
    const val MAX_ARCHIVE_BYTES = 675_000
    private const val MAGIC = 0x12D5819A
    private const val TIMESTAMPS_OFFSET = 112
    private const val GRADES_OFFSET = 1136

    fun safeId(id: String) = id.matches(Regex("[A-Za-z0-9_]{1,40}"))

    fun validProgress(bytes: ByteArray): Boolean {
        if (bytes.size != PROGRESS_BYTES) return false
        val input = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        return input.getInt(0) == MAGIC && input.getInt(36) in 0..16 &&
            input.getInt(40) in 0..128 && input.getInt(44) in -1..127
    }

    fun encode(games: Map<String, ByteArray>): ByteArray {
        require(games.size <= MAX_GAMES)
        val output = ByteArrayOutputStream()
        ZipOutputStream(output).use { zip ->
            games.toSortedMap().forEach { (id, progress) ->
                require(safeId(id) && validProgress(progress)) { "Invalid trophy progress" }
                zip.putNextEntry(ZipEntry("$id/$PROGRESS_NAME").apply { time = 0L })
                zip.write(progress)
                zip.closeEntry()
            }
        }
        return output.toByteArray().also { require(it.size <= MAX_ARCHIVE_BYTES) { "Trophy archive too large" } }
    }

    fun decode(archive: ByteArray): Map<String, ByteArray> {
        require(archive.size in 22..MAX_ARCHIVE_BYTES && archive[0] == 0x50.toByte() &&
            archive[1] == 0x4b.toByte()) { "Invalid trophy archive" }
        val result = linkedMapOf<String, ByteArray>()
        ZipInputStream(ByteArrayInputStream(archive)).use { zip ->
            while (true) {
                val entry = zip.nextEntry ?: break
                val parts = entry.name.split('/')
                require(!entry.isDirectory && parts.size == 2 && safeId(parts[0]) && parts[1] == PROGRESS_NAME)
                require(result.size < MAX_GAMES && !result.containsKey(parts[0]))
                val progress = ByteArray(PROGRESS_BYTES)
                var offset = 0
                while (offset < progress.size) {
                    val count = zip.read(progress, offset, progress.size - offset)
                    require(count > 0) { "Truncated trophy progress" }
                    offset += count
                }
                require(zip.read() == -1 && validProgress(progress)) { "Invalid trophy progress" }
                result[parts[0]] = progress
                zip.closeEntry()
            }
        }
        return result
    }

    /** Unlocks only accumulate across devices; preserve the earliest known unlock date. */
    fun merge(first: Map<String, ByteArray>, second: Map<String, ByteArray>): Map<String, ByteArray> {
        val result = first.mapValues { (id, bytes) ->
            require(safeId(id) && validProgress(bytes)); bytes.copyOf()
        }.toMutableMap()
        second.forEach { (id, bytes) ->
            require(safeId(id) && validProgress(bytes))
            val previous = result[id]
            result[id] = if (previous == null) bytes.copyOf() else mergeProgress(previous, bytes)
        }
        require(result.size <= MAX_GAMES)
        return result
    }

    private fun mergeProgress(first: ByteArray, second: ByteArray): ByteArray {
        val a = ByteBuffer.wrap(first).order(ByteOrder.LITTLE_ENDIAN)
        val b = ByteBuffer.wrap(second).order(ByteOrder.LITTLE_ENDIAN)
        // Prefer the more complete set metadata when an update added trophies.
        val result = (if (b.getInt(40) > a.getInt(40)) second else first).copyOf()
        val output = ByteBuffer.wrap(result).order(ByteOrder.LITTLE_ENDIAN)
        for (word in 0..3) output.putInt(4 + word * 4, a.getInt(4 + word * 4) or b.getInt(4 + word * 4))
        for (id in 0..127) {
            val flagOffset = 4 + id / 32 * 4
            val aUnlocked = a.getInt(flagOffset) and (1 shl (id % 32)) != 0
            val bUnlocked = b.getInt(flagOffset) and (1 shl (id % 32)) != 0
            if (aUnlocked || bUnlocked) {
                val aTime = if (aUnlocked) a.getLong(TIMESTAMPS_OFFSET + id * 8) else 0
                val bTime = if (bUnlocked) b.getLong(TIMESTAMPS_OFFSET + id * 8) else 0
                val timestamp = when {
                    aTime > 0 && bTime > 0 -> minOf(aTime, bTime)
                    aTime > 0 -> aTime
                    bTime > 0 -> bTime
                    else -> 0
                }
                output.putLong(TIMESTAMPS_OFFSET + id * 8, timestamp)
            }
            if (output.getInt(GRADES_OFFSET + id * 4) !in 1..4) {
                val grade = if (a.getInt(GRADES_OFFSET + id * 4) in 1..4) a.getInt(GRADES_OFFSET + id * 4)
                    else b.getInt(GRADES_OFFSET + id * 4)
                output.putInt(GRADES_OFFSET + id * 4, grade)
            }
        }
        return result
    }
}
