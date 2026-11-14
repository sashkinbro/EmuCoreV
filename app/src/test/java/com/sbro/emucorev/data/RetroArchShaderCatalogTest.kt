package com.sbro.emucorev.data

import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test

class RetroArchShaderCatalogTest {
    @Test fun reopensInstalledCatalogRecoversCorruptionAndRemembersRemovedPresets() = runBlocking {
        val temp = kotlin.io.path.createTempDirectory("shader-catalog-").toFile()
        try {
            val archive = File(temp, "official.zip")
            ZipOutputStream(archive.outputStream()).use { zip ->
                mapOf("pack/pass.slang" to "#pragma parameter X \"X\" 1 0 2\n",
                    "pack/crt/one.slangp" to "shaders = 1\nshader0 = ../pass.slang\n",
                    "pack/lcd/two.slangp" to "shaders = 1\nshader0 = ../pass.slang\n").forEach { (name, data) ->
                    zip.putNextEntry(ZipEntry(name)); zip.write(data.toByteArray()); zip.closeEntry()
                }
            }
            val root = File(temp, "installed")
            val cache = File(temp, "cache")
            val phases = mutableListOf<RetroArchShaderInstallProgress>()
            RetroArchShaderPackStore(root, cache).installArchive(archive, true, onProgress = phases::add)
            assertTrue(phases.any { it.stage == RetroArchShaderInstallStage.INSTALLING && it.transferredBytes > 0 })
            assertTrue(phases.any { it.stage == RetroArchShaderInstallStage.VALIDATING && it.processedFiles == 2 })
            val reopened = RetroArchShaderPackStore(root, cache)
            assertTrue(reopened.hasInstalledPack())
            val presets = reopened.listPresets()
            assertEquals(2, presets.size)
            assertSame("Reopening a tab must reuse the in-memory catalog", presets,
                RetroArchShaderPackStore(root, cache).listPresets())
            assertEquals(presets, RetroArchShaderCatalog.read(root))
            File(root, ".preset-catalog-v3").writeText("interrupted catalog")
            assertEquals(presets, RetroArchShaderPackStore(root, cache).listPresets())
            reopened.removePresets(setOf(presets.first().absolutePath)).getOrThrow()
            assertEquals(listOf(presets.last()), RetroArchShaderPackStore(root, cache).listPresets())
            assertTrue("Removing a preset does not uninstall the downloaded pack", reopened.hasInstalledPack())
        } finally { temp.deleteRecursively() }
    }
}
