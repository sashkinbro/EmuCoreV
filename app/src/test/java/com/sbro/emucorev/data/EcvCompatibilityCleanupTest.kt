package com.sbro.emucorev.data

import java.nio.file.Files
import org.junit.Assert.*
import org.junit.Test

class EcvCompatibilityCleanupTest {
    private val obsolete = listOf("app_compat_db.xml", "commercial_list.json", "app_compat_meta.properties",
        "app_compat_db.tmp.zip", "app_compat_db.tmp.xml", "commercial_list.tmp.json")

    @Test fun deletesOnlySixRegularFilesAndPreservesUnrelatedFilesDirectoriesAndOwnedCache() {
        val files = Files.createTempDirectory("ecv-cleanup").toFile()
        try {
            val old = files.resolve("compatibility").apply { mkdir() }
            obsolete.forEach { old.resolve(it).writeText("obsolete", Charsets.UTF_8) }
            old.resolve("unrelated.json").writeText("retained", Charsets.UTF_8)
            files.resolve("commercial_list.json").writeText("outside", Charsets.UTF_8)
            val cache = files.resolve("ecv_compatibility/cache-v1.json").apply {
                parentFile.mkdirs(); writeText("owned", Charsets.UTF_8)
            }
            cleanupObsoleteCompatibilityFiles(files)
            obsolete.forEach { assertFalse(it, old.resolve(it).exists()) }
            assertEquals("retained", old.resolve("unrelated.json").readText(Charsets.UTF_8))
            assertEquals("outside", files.resolve("commercial_list.json").readText(Charsets.UTF_8))
            assertEquals("owned", cache.readText(Charsets.UTF_8))
            old.resolve(obsolete.first()).mkdir()
            old.resolve("${obsolete.first()}/inside").writeText("retained", Charsets.UTF_8)
            cleanupObsoleteCompatibilityFiles(files)
            assertTrue(old.resolve("${obsolete.first()}/inside").isFile)
        } finally { files.deleteRecursively() }
    }

    @Test fun neverFollowsRootOrNamedFileSymlink() {
        val files = Files.createTempDirectory("ecv-cleanup-links").toFile()
        val outside = Files.createTempDirectory("ecv-cleanup-target").toFile()
        try {
            val external = outside.resolve(obsolete.first()).apply { writeText("private", Charsets.UTF_8) }
            val root = files.resolve("compatibility")
            try { Files.createSymbolicLink(root.toPath(), outside.toPath()) }
            catch (error: Exception) { org.junit.Assume.assumeNoException(error) }
            cleanupObsoleteCompatibilityFiles(files)
            assertTrue(Files.isSymbolicLink(root.toPath()))
            assertEquals("private", external.readText(Charsets.UTF_8))
            Files.delete(root.toPath()); root.mkdir()
            val target = root.resolve(obsolete.first())
            Files.createSymbolicLink(target.toPath(), external.toPath())
            cleanupObsoleteCompatibilityFiles(files)
            assertTrue(Files.isSymbolicLink(target.toPath()))
            assertEquals("private", external.readText(Charsets.UTF_8))
        } finally { files.deleteRecursively(); outside.deleteRecursively() }
    }
}
