package com.sbro.emucorev.data

import java.io.File
import java.nio.file.Files
import java.security.MessageDigest
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(manifest = Config.NONE, sdk = [29])
class VitaCoverResolverTest {
    @Test fun cachedOnlyLookupNeverDownloadsOrDisplaysAnIcon() = runBlocking { fixture { root ->
        val icon = File(root, "icon.png").apply { writeBytes(bytes) }
        val cache = CoverDiskCache(File(root, "cache"), { _, _, _ -> error("cache warming must never fetch") }, { true })
        val resolver = VitaCoverResolver(index, cache) { File(it).isFile }
        assertNull(resolver.resolve("PCSA00001", null, null, icon.path, VitaCoverStyle.THREE_D, cachedOnly = true).path)
    } }
    private val bytes = "cover fixture".toByteArray()
    private val hash = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
    private val index = VitaCoverIndex.fromJson("""{"schema_version":2,"asset_revision":"${"a".repeat(40)}",
      "games":{"42":{"path":"covers/42.webp","path_3d":"covers/3d/42.webp","sha256":"$hash","sha256_3d":"$hash","source_image_id":"coABC","source_url":"https://images.igdb.com/igdb/image/upload/t_original/coABC.jpg"}},"serials":{"PCSA00001":"42"}}""")
    private suspend fun fixture(block: suspend (File) -> Unit) {
        val root = Files.createTempDirectory("vita-resolve-test-").toFile()
        try { block(root) } finally { root.deleteRecursively() }
    }
    @Test fun explicitCustomWinsWithoutFetchingEvenForUnknownSerial() = runBlocking { fixture { root ->
        val custom = File(root, "custom.webp").apply { writeBytes(bytes) }
        val cache = CoverDiskCache(File(root, "cache"), { _, _, _ -> error("custom art must win") }, { true })
        val resolver = VitaCoverResolver(index, cache) { File(it).isFile }
        assertEquals(custom.path, resolver.resolve("UNKNOWN", null, custom.path, null, VitaCoverStyle.THREE_D).path)
        cache.clear(); assertTrue(custom.isFile)
    } }
    @Test fun iconPassedAsCandidateDoesNotOverrideManagedCover() = runBlocking { fixture { root ->
        val icon = File(root, "icon.png").apply { writeBytes(bytes) }
        val cache = CoverDiskCache(File(root, "cache"), { _, temp, _ -> temp.writeBytes(bytes); CoverFetchResult.DOWNLOADED }, { true })
        val resolver = VitaCoverResolver(index, cache) { File(it).isFile }
        val result = resolver.resolve("PCSA00001", null, icon.path, icon.toURI().toString(), VitaCoverStyle.THREE_D)
        assertTrue(result.is3d); assertNotEquals(icon.path, result.path); assertNotNull(result.publicUrl)
    } }
    @Test fun downloadFailureLeavesTitlePlaceholderInsteadOfOldIcon() = runBlocking { fixture { root ->
        val icon = File(root, "icon.png").apply { writeBytes(bytes) }
        val cache = CoverDiskCache(File(root, "cache"), { _, _, _ -> CoverFetchResult.RETRYABLE_FAILURE }, { true })
        val resolver = VitaCoverResolver(index, cache) { File(it).isFile }
        val result = resolver.resolve("PCSA00001", null, null, icon.path, VitaCoverStyle.THREE_D)
        assertNull(result.path); assertFalse(result.is3d)
    } }
    @Test fun unknownSerialNeverUsesGameIconAsCover() = runBlocking { fixture { root ->
        val icon = File(root, "icon.png").apply { writeBytes(bytes) }
        val cache = CoverDiskCache(File(root, "cache"), { _, _, _ -> error("unknown serial must not download") }, { true })
        val resolver = VitaCoverResolver(index, cache) { File(it).isFile }
        assertNull(resolver.resolve("UNKNOWN", null, icon.path, icon.toURI().toString(), VitaCoverStyle.THREE_D).path)
    } }
    @Test fun cachedManagedCandidateCannotBecomeCustomOrBypassIdentity() = runBlocking { fixture { root ->
        val cacheRoot = File(root, "cache")
        val managed = File(cacheRoot, "old.webp").apply { parentFile!!.mkdirs(); writeBytes(bytes) }
        val cache = CoverDiskCache(cacheRoot, { _, _, _ -> error("unknown ID must not download") }, { true })
        val resolver = VitaCoverResolver(index, cache) { File(it).isFile }
        assertNull(resolver.resolve("PCSA99999", 42, managed.path, null, VitaCoverStyle.THREE_D).path)
    } }
    @Test fun legacyIgdbUrlResolvesOnlyToOwnRepository() = runBlocking { fixture { root ->
        val cache = CoverDiskCache(File(root, "cache"), { asset, temp, _ ->
            assertTrue(asset.url.startsWith("https://raw.githubusercontent.com/sashkinbro/EmuCoreV-Covers/"))
            temp.writeBytes(bytes); CoverFetchResult.DOWNLOADED
        }, { true })
        val resolver = VitaCoverResolver(index, cache) { false }
        val result = resolver.resolve(null, null, "https://images.igdb.com/igdb/image/upload/t_cover_big/coABC.jpg", null, VitaCoverStyle.FLAT)
        assertNotNull(result.path); assertFalse(result.is3d); assertEquals(42L, result.igdbId)
    } }
    @Test fun unrelatedRemoteUrlsAreNeverFetchedOrUsedAsLocalCustom() = runBlocking { fixture { root ->
        val cache = CoverDiskCache(File(root, "cache"), { _, _, _ -> error("no arbitrary network") }, { true })
        val resolver = VitaCoverResolver(index, cache) { false }
        assertNull(resolver.resolve(null, null, "https://elsewhere.test/art.jpg", null, VitaCoverStyle.FLAT).path)
    } }
}
