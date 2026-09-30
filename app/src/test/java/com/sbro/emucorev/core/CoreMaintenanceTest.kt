package com.sbro.emucorev.core

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CoreMaintenanceTest {

    @Test
    fun `reset clears regenerable entries and keeps user data`() {
        val root = Files.createTempDirectory("core-maintenance").toFile()
        try {
            val config = File(root, "config").apply { mkdirs() }
            File(config, "config.yml").writeText("native config")
            File(root, "config.yml").writeText("core settings")
            val cache = File(root, "cache").apply { mkdirs() }
            File(cache, "shaders.bin").writeText("shaders")
            File(File(root, "shaderlog").apply { mkdirs() }, "log.txt").writeText("log")
            File(File(root, "texturelog").apply { mkdirs() }, "tex.txt").writeText("texture")

            val patches = File(root, "patch").apply { mkdirs() }
            val patchFile = File(patches, "patches.yml").apply { writeText("patches") }
            val playTime = File(root, "play_time.json").apply { writeText("time") }

            val deletedFiles = clearRegenerableCoreState(root)

            assertEquals(5, deletedFiles)
            assertFalse(File(root, "config").exists())
            assertFalse(File(root, "config.yml").exists())
            assertFalse(File(root, "cache").exists())
            assertFalse(File(root, "shaderlog").exists())
            assertFalse(File(root, "texturelog").exists())
            assertTrue(patchFile.isFile)
            assertEquals("patches", patchFile.readText())
            assertTrue(playTime.isFile)
            assertEquals("time", playTime.readText())
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun `reset tolerates a runtime root without regenerable entries`() {
        val root = Files.createTempDirectory("core-maintenance-empty").toFile()
        try {
            File(root, "patch").mkdirs()
            assertEquals(0, clearRegenerableCoreState(root))
        } finally {
            root.deleteRecursively()
        }
    }
}
