package com.sbro.emucorev.data

import kotlinx.coroutines.launch
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.job
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.yield
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.io.FileOutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

class RetroArchShaderPackStoreTest {
    @Test
    fun importedLenovoRedTintPresetIsListedWithItsDeclaredParameter() = runBlocking {
        val temp = kotlin.io.path.createTempDirectory("shader-lenovo-red-tint-").toFile()
        try {
            val archive = File(temp, "red-tint.zip")
            ZipOutputStream(FileOutputStream(archive)).use { zip ->
                for (name in listOf("ecv_red_tint.slang", "ecv_red_tint.slangp")) {
                    val resource = javaClass.getResourceAsStream("/shaders/red-tint/$name")
                        ?: error("Missing regression fixture $name")
                    zip.putNextEntry(ZipEntry(name))
                    resource.use { it.copyTo(zip) }
                    zip.closeEntry()
                }
            }
            val store = RetroArchShaderPackStore(File(temp, "installed"), File(temp, "cache"))

            assertTrue("Red-tint pack should install", store.installArchive(archive, official = false) == 1)
            val presets = store.listPresets()
            assertTrue("Imported red-tint preset disappeared from the installed catalog: $presets", presets.size == 1)
            assertTrue(presets.single().label.endsWith("ecv_red_tint"))
            assertTrue(presets.single().parameters.any { it.id == "RedTint" })
        } finally {
            temp.deleteRecursively()
        }
    }

    @Test
    fun cancellationWhileWaitingForStoreLockLeavesInstalledPackUntouched() = runBlocking {
        val temp = kotlin.io.path.createTempDirectory("shader-queued-cancel-").toFile()
        try {
            val root = File(temp, "installed")
            val cache = File(temp, "cache")
            val archive = File(temp, "pack.zip")
            writeValidPack(archive, "new")
            val mutex = Mutex(locked = true)
            val store = RetroArchShaderPackStore(root, cache, mutex)
            val install = launch {
                store.installArchive(archive, official = true)
            }
            yield()
            assertTrue(install.isActive)
            install.cancel()
            install.join()
            mutex.unlock()

            assertTrue(install.isCancelled)
            assertFalse(root.exists())
            assertTrue(cache.listFiles().orEmpty().none { it.name.startsWith("shader-stage-") })
        } finally {
            temp.deleteRecursively()
        }
    }

    @Test
    fun cancellationDuringArchiveExtractionRemovesPartialStaging() = runBlocking {
        val temp = kotlin.io.path.createTempDirectory("shader-extract-cancel-").toFile()
        try {
            val root = File(temp, "installed")
            val cache = File(temp, "cache")
            val archive = File(temp, "large.zip")
            writeLargeArchive(archive)
            val store = RetroArchShaderPackStore(root, cache)
            var chunkCount = 0
            val install = launch {
                val job = currentCoroutineContext().job
                store.installArchive(archive, official = false, onExtractionChunk = {
                    if (++chunkCount == 1) job.cancel()
                })
            }
            install.join()

            assertTrue(install.isCancelled)
            assertFalse(root.exists())
            assertTrue(cache.listFiles().orEmpty().none { it.name.startsWith("shader-stage-") })
        } finally {
            temp.deleteRecursively()
        }
    }

    @Test
    fun traversalArchiveCannotReplaceAnInstalledPack() = runBlocking {
        val temp = kotlin.io.path.createTempDirectory("shader-path-reject-").toFile()
        try {
            val root = File(temp, "installed")
            val cache = File(temp, "cache")
            val validArchive = File(temp, "valid.zip")
            val unsafeArchive = File(temp, "unsafe.zip")
            writeValidPack(validArchive, "original")
            ZipOutputStream(FileOutputStream(unsafeArchive)).use { zip ->
                write(zip, "pack/shaders/escape.slang", "void main() {}\n".toByteArray())
                write(zip, "pack/shaders/../escape.slangp", "shaders = 1\nshader0 = \"escape.slang\"\n".toByteArray())
            }
            val store = RetroArchShaderPackStore(root, cache)
            store.installArchive(validArchive, official = false)

            val result = runCatching { store.installArchive(unsafeArchive, official = false) }

            assertTrue(result.isFailure)
            assertTrue(store.listPresets().any { it.label.endsWith("original") })
            assertFalse(File(temp, "installed/shaders/escape.slangp").exists())
            assertTrue(cache.listFiles().orEmpty().none { it.name.startsWith("shader-stage-") })
        } finally {
            temp.deleteRecursively()
        }
    }

    private fun writeValidPack(archive: File, sourceName: String) {
        ZipOutputStream(FileOutputStream(archive)).use { zip ->
            write(zip, "pack/shaders/$sourceName.slang", "void main() {}\n".toByteArray())
            write(zip, "pack/shaders/$sourceName.slangp", "shaders = 1\nshader0 = \"$sourceName.slang\"\n".toByteArray())
        }
    }

    private fun writeLargeArchive(archive: File) {
        ZipOutputStream(FileOutputStream(archive)).use { zip ->
            zip.putNextEntry(ZipEntry("pack/large.bin"))
            zip.write(ByteArray(4 * 1024 * 1024))
            zip.closeEntry()
        }
    }

    private fun write(zip: ZipOutputStream, name: String, bytes: ByteArray) {
        zip.putNextEntry(ZipEntry(name))
        zip.write(bytes)
        zip.closeEntry()
    }
}
