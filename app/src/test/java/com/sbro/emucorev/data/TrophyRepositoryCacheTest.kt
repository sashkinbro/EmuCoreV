package com.sbro.emucorev.data

import android.app.Application
import com.sbro.emucorev.core.EmulatorStorage
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

/** Verifies the in-game achievements tab only re-parses when trophy files change. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], application = Application::class)
class TrophyRepositoryCacheTest {
    private lateinit var context: Application
    private lateinit var repository: TrophyRepository
    private lateinit var progressFile: File

    @Before
    fun setUp() {
        context = RuntimeEnvironment.getApplication()
        repository = TrophyRepository()
        val vitaRoot = EmulatorStorage.vitaRoot(context)
        val appDir = File(vitaRoot, "ux0/app/PCSA00001")
        writeSfo(
            File(appDir, "sce_sys/param.sfo"),
            linkedMapOf("TITLE_ID" to "PCSA00001", "TITLE" to "Test Game")
        )
        File(appDir, "sce_sys/trophy/NPWR00001_00/TROPHY.TRP").apply {
            parentFile?.mkdirs()
            writeBytes(ByteArray(0))
        }
        val confDir = File(vitaRoot, "ux0/user/00/trophy/conf/NPWR00001_00").apply { mkdirs() }
        File(confDir, "TROPCONF.SFM").writeText(TROPCONF)
        progressFile = File(vitaRoot, "ux0/user/00/trophy/data/NPWR00001_00/TROPUSR.DAT").apply {
            parentFile?.mkdirs()
            writeBytes(progressBytes(setOf(0)))
        }
    }

    @Test
    fun returnsCachedSetsUntilProgressChanges() {
        assertNull(repository.cachedForTitle(context, "PCSA00001"))

        val first = repository.loadForTitle(context, "PCSA00001")
        assertEquals(1, first.size)
        assertEquals(2, first[0].trophyCount)
        assertEquals(1, first[0].unlockedCount)
        assertEquals("First", first[0].trophies.first().name)
        assertNotNull(repository.cachedForTitle(context, "PCSA00001"))

        // Unchanged files: still one unlocked trophy, served from cache.
        assertEquals(1, repository.loadForTitle(context, "PCSA00001")[0].unlockedCount)

        // Unlocking the second trophy must invalidate the cache and re-parse.
        progressFile.writeBytes(progressBytes(setOf(0, 1)))
        progressFile.setLastModified(System.currentTimeMillis() + 2_000)
        assertEquals(2, repository.loadForTitle(context, "PCSA00001")[0].unlockedCount)
    }

    private fun writeSfo(file: File, values: Map<String, String>) {
        val keys = values.keys.toList()
        val keyTable = StringBuilder()
        val keyOffsets = mutableMapOf<String, Int>()
        keys.forEach { key ->
            keyOffsets[key] = keyTable.length
            keyTable.append(key).append('\u0000')
        }
        val keyBytes = keyTable.toString().toByteArray(Charsets.UTF_8)
        val dataStream = ByteArrayOutputStream()
        val dataOffsets = mutableMapOf<String, Int>()
        keys.forEach { key ->
            val value = values.getValue(key).toByteArray(Charsets.UTF_8) + byteArrayOf(0)
            dataOffsets[key] = dataStream.size()
            dataStream.write(value)
            while (dataStream.size() % 4 != 0) dataStream.write(0)
        }
        val dataBytes = dataStream.toByteArray()
        val keyTableStart = 20 + 16 * keys.size
        val dataTableStart = keyTableStart + keyBytes.size
        val out = ByteBuffer.allocate(dataTableStart + dataBytes.size).order(ByteOrder.LITTLE_ENDIAN)
        out.putInt(0x46535000)
        out.putShort(0x0101)
        out.putShort(0)
        out.putInt(keyTableStart)
        out.putInt(dataTableStart)
        out.putInt(keys.size)
        keys.forEach { key ->
            out.putShort(keyOffsets.getValue(key).toShort())
            out.put(0)
            out.put(0)
            out.putInt(values.getValue(key).toByteArray(Charsets.UTF_8).size + 1)
            out.putInt(0)
            out.putInt(dataOffsets.getValue(key))
        }
        out.put(keyBytes)
        out.put(dataBytes)
        file.parentFile?.mkdirs()
        file.writeBytes(out.array())
    }

    private fun progressBytes(unlockedIds: Set<Int>): ByteArray {
        val maxTrophies = 128
        val flagWords = maxTrophies / 32
        val buffer = ByteBuffer.allocate(
            4 + flagWords * 4 + flagWords * 4 + 3 * 4 + 16 * 4 + maxTrophies * 8 + maxTrophies * 4
        ).order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(0x12D5819A.toInt())
        val flags = IntArray(flagWords)
        unlockedIds.forEach { id -> flags[id / 32] = flags[id / 32] or (1 shl (id % 32)) }
        flags.forEach { buffer.putInt(it) }
        repeat(flagWords) { buffer.putInt(0) }
        repeat(3) { buffer.putInt(0) }
        repeat(16) { buffer.putInt(0) }
        repeat(maxTrophies) { buffer.putLong(0L) }
        repeat(maxTrophies) { buffer.putInt(0) }
        return buffer.array()
    }

    private companion object {
        val TROPCONF = """
            <?xml version="1.0" encoding="utf-8"?>
            <trophyconf version="1.00" npcommid="NPWR00001_00">
              <title-name>Test Trophies</title-name>
              <title-detail>Details</title-detail>
              <trophy id="0" gid="0" ttype="B" hidden="no">
                <name>First</name>
                <detail>Do a thing</detail>
              </trophy>
              <trophy id="1" gid="0" ttype="S" hidden="no">
                <name>Second</name>
                <detail>Do another thing</detail>
              </trophy>
            </trophyconf>
        """.trimIndent()
    }
}
