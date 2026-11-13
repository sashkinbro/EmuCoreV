package com.sbro.emucorev.data

import java.io.ByteArrayInputStream
import java.nio.file.Files
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.assertThrows
import org.junit.Test

class EcvCompatibilityRepositoryTest {
    @Test fun handlesZeroAndOneByteShortReadsAndClosesWithoutSpinning() {
        val directory = Files.createTempDirectory("ecv-short").toFile()
        var closed = false
        val bytes = EcvCompatibilityParserTestFixture.EMPTY_DATASET.toByteArray()
        val source = object : ByteArrayInputStream(bytes) {
            var zero = true
            override fun read(buffer: ByteArray, offset: Int, length: Int): Int {
                zero = !zero
                return if (!zero) 0 else super.read(buffer, offset, minOf(1, length))
            }
            override fun close() { closed = true; super.close() }
        }
        try {
            val repository = EcvCompatibilityDatasetRepository(directory.resolve("cache.json"), bundledSource = { source })
            assertEquals(1, repository.load().datasetVersion)
            assertTrue(closed)
        } finally { directory.deleteRecursively() }
    }

    @Test fun symbolicCacheOrParentIsNeverReadOrReplaced() {
        val directory = Files.createTempDirectory("ecv-links").toFile()
        val outside = Files.createTempDirectory("ecv-outside").toFile()
        try {
            val external = outside.resolve("cache.json").apply {
                writeText(EcvCompatibilityParserTestFixture.DATASET_WITH_ONE_PASS, Charsets.UTF_8)
            }
            val link = directory.resolve("cache.json")
            try { Files.createSymbolicLink(link.toPath(), external.toPath()) }
            catch (error: Exception) { org.junit.Assume.assumeNoException(error) }
            val repository = EcvCompatibilityDatasetRepository(link, bundledSource = {
                ByteArrayInputStream(EcvCompatibilityParserTestFixture.EMPTY_DATASET.toByteArray())
            })
            assertEquals(1, repository.load().datasetVersion)
            assertThrows(IllegalArgumentException::class.java) {
                repository.replaceCache(ByteArrayInputStream(EcvCompatibilityParserTestFixture.EMPTY_DATASET.toByteArray()))
            }
            assertTrue(Files.isSymbolicLink(link.toPath()))
            assertEquals(2, EcvCompatibilityParser().parse(external.readBytes()).datasetVersion)
            Files.delete(link.toPath())
            val parent = directory.resolve("linked")
            Files.createSymbolicLink(parent.toPath(), outside.toPath())
            val parentRepo = EcvCompatibilityDatasetRepository(parent.resolve("cache.json"), bundledSource = {
                ByteArrayInputStream(EcvCompatibilityParserTestFixture.EMPTY_DATASET.toByteArray())
            })
            assertEquals(1, parentRepo.load().datasetVersion)
            assertThrows(IllegalArgumentException::class.java) {
                parentRepo.replaceCache(ByteArrayInputStream(EcvCompatibilityParserTestFixture.EMPTY_DATASET.toByteArray()))
            }
        } finally { directory.deleteRecursively(); outside.deleteRecursively() }
    }

    @Test fun readsAtMostTheDocumentBudgetPlusOneByteBeforeRejectingAndClosesSource() {
        val directory = Files.createTempDirectory("ecv-bounds").toFile()
        var consumed = 0
        var closed = false
        val source = object : java.io.InputStream() {
            override fun read(): Int { consumed++; return ' '.code }
            override fun read(bytes: ByteArray, offset: Int, length: Int): Int {
                consumed += length; bytes.fill(' '.code.toByte(), offset, offset + length); return length
            }
            override fun close() { closed = true }
        }
        try {
            val repository = EcvCompatibilityDatasetRepository(directory.resolve("cache.json"), bundledSource = {
                ByteArrayInputStream(EcvCompatibilityParserTestFixture.EMPTY_DATASET.toByteArray())
            })
            assertThrows(IllegalArgumentException::class.java) { repository.replaceCache(source) }
            assertEquals(EcvCompatibilityParser.MAX_DOCUMENT_BYTES + 1, consumed)
            assertTrue(closed)
        } finally { directory.deleteRecursively() }
    }

    @Test
    fun loadsBundledOfflineDatasetAndNeverReadsLegacyUpstreamFiles() {
        val directory = Files.createTempDirectory("ecv-compatibility").toFile()
        try {
            FileFixture(directory, "legacy-test-data.xml", "<synthetic-fixture />")
            FileFixture(directory, "legacy-test-data.json", "{\"fixture\":true}")
            val bundled = EcvCompatibilityParserTestFixture.EMPTY_DATASET
            val repository = EcvCompatibilityDatasetRepository(
                cacheFile = directory.resolve("ecv_compatibility/cache-v1.json"),
                bundledSource = { ByteArrayInputStream(bundled.toByteArray()) }
            )

            val loaded = repository.load()

            assertTrue(loaded.runsFor("PCZZ99999").isEmpty())
            assertEquals(1, loaded.schemaVersion)
        } finally {
            directory.deleteRecursively()
        }
    }

    @Test
    fun returnsValidatedCacheAndKeepsItUnchangedWhenReplacementIsInvalid() {
        val directory = Files.createTempDirectory("ecv-compatibility-cache").toFile()
        try {
            val cache = directory.resolve("ecv_compatibility/cache-v1.json")
            val repository = EcvCompatibilityDatasetRepository(
                cacheFile = cache,
                bundledSource = { ByteArrayInputStream(EcvCompatibilityParserTestFixture.EMPTY_DATASET.toByteArray()) }
            )
            val valid = EcvCompatibilityParserTestFixture.DATASET_WITH_ONE_PASS
            repository.replaceCache(ByteArrayInputStream(valid.toByteArray()))
            val firstBytes = cache.readBytes()
            val replacement = valid
                .replace("\"dataset_version\":2", "\"dataset_version\":3")
                .replace("fixture-device-b-run", "fixture-device-b-run-2")
                .replace("\"tested_on\":\"2026-11-12\"", "\"tested_on\":\"2026-11-13\"")
            val replaced = repository.replaceCache(ByteArrayInputStream(replacement.toByteArray()))
            assertEquals(3, replaced.datasetVersion)
            assertTrue(!firstBytes.contentEquals(cache.readBytes()))
            val oldBytes = cache.readBytes()

            assertEquals(EcvCompatibilityStatus.PASSED, repository.load().runsFor("PCZZ99999").single().results.gameplay.status)
            assertThrows(IllegalArgumentException::class.java) {
                repository.replaceCache(ByteArrayInputStream("{\"schema_version\":99}".toByteArray()))
            }

            assertArrayEquals(oldBytes, cache.readBytes())
            assertEquals(3, repository.load().datasetVersion)
            assertEquals(EcvCompatibilityStatus.PASSED, repository.load().runsFor("PCZZ99999").single().results.gameplay.status)
        } finally {
            directory.deleteRecursively()
        }
    }

    @Test
    fun fallsBackToBundledDataWhenCacheIsCorrupt() {
        val directory = Files.createTempDirectory("ecv-compatibility-corrupt").toFile()
        try {
            val cache = directory.resolve("ecv_compatibility/cache-v1.json")
            cache.parentFile.mkdirs()
            cache.writeText("{broken")
            val repository = EcvCompatibilityDatasetRepository(
                cacheFile = cache,
                bundledSource = { ByteArrayInputStream(EcvCompatibilityParserTestFixture.DATASET_WITH_ONE_PASS.toByteArray()) }
            )

            val loaded = repository.load()

            assertEquals(EcvCompatibilityStatus.PASSED, loaded.runsFor("PCZZ99999").single().results.gameplay.status)
            assertEquals("{broken", cache.readText())
        } finally {
            directory.deleteRecursively()
        }
    }

    private fun FileFixture(directory: java.io.File, name: String, content: String) {
        directory.resolve(name).writeText(content)
    }
}

private object EcvCompatibilityParserTestFixture {
    const val EMPTY_DATASET = """
        {"schema_version":1,"dataset_version":1,"source":"EmuCoreV","generated_on":"2026-11-13","records":[]}
    """

    const val DATASET_WITH_ONE_PASS = """
        {
          "schema_version":1,
          "dataset_version":2,
          "source":"EmuCoreV",
          "generated_on":"2026-11-13",
          "records":[{
            "title_id":"PCZZ99999",
            "runs":[{
              "run_id":"fixture-device-b-run",
              "device":{"manufacturer":"Fixture Maker","model":"Device B","soc":"Synthetic SoC B","gpu":"Synthetic GPU B"},
              "app_version":"test-app-1",
              "core_version":"test-core-1",
              "tested_on":"2026-11-12",
              "renderer_settings":{"backend":"test-backend"},
              "results":{
                "gameplay":{"status":"passed","evidence":["fixture-gameplay"]},
                "graphics":{"status":"not_tested","evidence":[]},
                "audio":{"status":"not_tested","evidence":[]},
                "quick_save":{"status":"passed","evidence":["fixture-save"]},
                "warm_load":{"status":"passed","evidence":["fixture-warm-load"]},
                "cold_load":{"status":"passed","evidence":["fixture-cold-load"]}
              }
            }]
          }]
        }
    """
}
