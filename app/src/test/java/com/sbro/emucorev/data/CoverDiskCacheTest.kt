package com.sbro.emucorev.data

import java.io.File
import java.nio.file.Files
import java.security.MessageDigest
import java.util.concurrent.atomic.AtomicInteger
import kotlinx.coroutines.*
import org.junit.Assert.*
import org.junit.Test

class CoverDiskCacheTest {
    @Test fun cachedCoverDoesNotWaitBehindFourNetworkMisses() = runBlocking { fixture { root ->
        val expected = stored(root)
        val release = CompletableDeferred<Unit>()
        val started = CompletableDeferred<Unit>()
        val starts = AtomicInteger()
        val cache = CoverDiskCache(root, { _, _, _ ->
            if (starts.incrementAndGet() == 4) started.complete(Unit)
            release.await()
            CoverFetchResult.RETRYABLE_FAILURE
        }, { true })
        val jobs = (101L..104L).map { id ->
            launch { cache.resolve(asset().copy(igdbId = id, relativePath = "covers/3d/$id.webp")) }
        }
        try {
            started.await()
            val cached = withTimeoutOrNull(300) { cache.resolve(asset()) }
            assertEquals("A verified disk hit must bypass downloads", expected, cached)
        } finally {
            release.complete(Unit)
            jobs.joinAll()
        }
    } }
    private val valid = "small verified public image fixture".toByteArray()
    private fun hash(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
    private fun asset(style: VitaCoverStyle = VitaCoverStyle.THREE_D, revision: String = "a".repeat(40)) =
        VitaCoverAsset(42, style, revision, "covers/${if (style == VitaCoverStyle.THREE_D) "3d/" else ""}42.webp", hash(valid))
    private suspend fun fixture(block: suspend (File) -> Unit) {
        val root = Files.createTempDirectory("vita-cover-test-").toFile()
        try { withTimeout(2_000) { block(root) } } finally { root.deleteRecursively() }
    }
    private fun stored(root: File, asset: VitaCoverAsset = asset()): File = File(root, asset.cacheKey).apply {
        parentFile!!.mkdirs(); writeBytes(valid)
    }

    @Test fun validCacheWorksOfflineWithoutFetch() = runBlocking { fixture { root ->
        val expected = stored(root)
        val cache = CoverDiskCache(root, { _, _, _ -> error("must stay offline") }, { true })
        assertEquals(expected.canonicalFile, cache.resolve(asset())?.canonicalFile)
    } }
    @Test fun corruptCacheIsReplacedOnlyAfterHashAndImageValidation() = runBlocking { fixture { root ->
        stored(root).writeText("corrupt")
        val cache = CoverDiskCache(root, { _, temp, _ -> temp.writeBytes(valid); CoverFetchResult.DOWNLOADED }, { it.readBytes().contentEquals(valid) })
        assertArrayEquals(valid, cache.resolve(asset())!!.readBytes())
        assertTrue(root.walk().none { it.extension == "tmp" })
    } }
    @Test fun hashFailureNeverPromotesAndRetries() = runBlocking { fixture { root ->
        val count = AtomicInteger()
        val cache = CoverDiskCache(root, { _, temp, _ ->
            temp.writeBytes(if (count.incrementAndGet() == 1) "bad".toByteArray() else valid)
            CoverFetchResult.DOWNLOADED
        }, { true })
        assertNull(cache.resolve(asset()))
        assertTrue(root.walk().none { it.isFile })
        assertNotNull(cache.resolve(asset()))
        assertEquals(2, count.get())
    } }
    @Test fun imageValidationFailureNeverPromotes() = runBlocking { fixture { root ->
        val cache = CoverDiskCache(root, { _, temp, _ -> temp.writeBytes(valid); CoverFetchResult.DOWNLOADED }, { false })
        assertNull(cache.resolve(asset()))
        assertTrue(root.walk().none { it.isFile })
    } }
    @Test fun transientFailureIsRetriedImmediately() = runBlocking { fixture { root ->
        var calls = 0
        val cache = CoverDiskCache(root, { _, temp, _ ->
            if (++calls == 1) CoverFetchResult.RETRYABLE_FAILURE
            else { temp.writeBytes(valid); CoverFetchResult.DOWNLOADED }
        }, { true })
        assertNull(cache.resolve(asset())); assertNotNull(cache.resolve(asset())); assertEquals(2, calls)
    } }
    @Test fun confirmedMissExpiresAndNewRevisionIsIndependent() = runBlocking { fixture { root ->
        var calls = 0; var time = 10L
        val cache = CoverDiskCache(root, { _, _, _ -> calls++; CoverFetchResult.NOT_FOUND }, { true }, { time })
        assertNull(cache.resolve(asset())); assertNull(cache.resolve(asset())); assertEquals(1, calls)
        assertNull(cache.resolve(asset(revision = "b".repeat(40)))); assertEquals(2, calls)
        time += 31 * 60 * 1000L
        assertNull(cache.resolve(asset())); assertEquals(3, calls)
    } }
    @Test fun concurrentSuccessfulCallersShareOneDownload() = runBlocking { fixture { root ->
        val calls = AtomicInteger()
        val cache = CoverDiskCache(root, { _, temp, _ ->
            calls.incrementAndGet(); delay(80); temp.writeBytes(valid); CoverFetchResult.DOWNLOADED
        }, { true })
        val paths = coroutineScope { (1..20).map { async { cache.resolve(asset()) } }.awaitAll() }
        assertTrue(paths.all { it != null }); assertEquals(1, calls.get())
    } }
    @Test fun concurrentFailuresShareOneAttemptButNextCallRetries() = runBlocking { fixture { root ->
        val calls = AtomicInteger()
        val cache = CoverDiskCache(root, { _, _, _ -> calls.incrementAndGet(); delay(80); CoverFetchResult.RETRYABLE_FAILURE }, { true })
        coroutineScope { (1..12).map { async { cache.resolve(asset()) } }.awaitAll() }
        assertEquals(1, calls.get())
        assertNull(cache.resolve(asset())); assertEquals(2, calls.get())
    } }
    @Test fun cancellationRemovesTempAndAllowsRetry() = runBlocking { fixture { root ->
        val entered = CompletableDeferred<Unit>()
        var calls = 0
        val cache = CoverDiskCache(root, { _, temp, _ ->
            if (++calls == 1) { temp.writeText("partial"); entered.complete(Unit); awaitCancellation() }
            else { temp.writeBytes(valid); CoverFetchResult.DOWNLOADED }
        }, { true })
        val job = launch { cache.resolve(asset()) }
        entered.await(); job.cancelAndJoin()
        assertTrue(root.walk().none { it.isFile })
        assertNotNull(cache.resolve(asset()))
    } }
    @Test fun activeWaiterRetriesWhenOnlyFlightOwnerIsCancelled() = runBlocking { fixture { root ->
        val entered = CompletableDeferred<Unit>()
        var calls = 0
        val cache = CoverDiskCache(root, { _, temp, _ ->
            if (++calls == 1) { temp.writeText("partial"); entered.complete(Unit); awaitCancellation() }
            temp.writeBytes(valid)
            CoverFetchResult.DOWNLOADED
        }, { true }, ioDispatcher = Dispatchers.Unconfined)
        val owner = launch(start = CoroutineStart.UNDISPATCHED) { cache.resolve(asset()) }
        entered.await()
        // Unconfined I/O and undispatched start deterministically join the existing flight.
        val waiter = async(start = CoroutineStart.UNDISPATCHED) { cache.resolve(asset()) }
        assertFalse(waiter.isCompleted)
        owner.cancelAndJoin()
        val resolved = waiter.await()
        assertFalse(waiter.isCancelled)
        assertNotNull(resolved)
        assertArrayEquals(valid, resolved!!.readBytes())
        assertEquals(2, calls)
        assertTrue(root.walk().none { it.extension == "tmp" })
    } }
    @Test fun cancellingAWaiterDoesNotCancelOwnerOrOtherWaiters() = runBlocking { fixture { root ->
        val release = CompletableDeferred<Unit>()
        var calls = 0
        val cache = CoverDiskCache(root, { _, temp, _ ->
            calls++; release.await(); temp.writeBytes(valid); CoverFetchResult.DOWNLOADED
        }, { true }, ioDispatcher = Dispatchers.Unconfined)
        val owner = async(start = CoroutineStart.UNDISPATCHED) { cache.resolve(asset()) }
        val cancelledWaiter = async(start = CoroutineStart.UNDISPATCHED) { cache.resolve(asset()) }
        val activeWaiter = async(start = CoroutineStart.UNDISPATCHED) { cache.resolve(asset()) }
        cancelledWaiter.cancelAndJoin(); release.complete(Unit)
        assertNotNull(owner.await()); assertNotNull(activeWaiter.await()); assertEquals(1, calls)
    } }
    @Test fun cancelledOwnerRetryStopsAtRealMissOrTransientFailure() = runBlocking { fixture { root ->
        for (status in listOf(CoverFetchResult.NOT_FOUND, CoverFetchResult.RETRYABLE_FAILURE)) {
            var calls = 0
            val cache = CoverDiskCache(File(root, status.name), { _, temp, _ ->
                when (++calls) {
                    1 -> awaitCancellation()
                    2 -> status
                    else -> { temp.writeBytes(valid); CoverFetchResult.DOWNLOADED }
                }
            }, { true }, ioDispatcher = Dispatchers.Unconfined)
            val owner = launch(start = CoroutineStart.UNDISPATCHED) { cache.resolve(asset()) }
            val waiter = async(start = CoroutineStart.UNDISPATCHED) { cache.resolve(asset()) }
            owner.cancelAndJoin()
            assertNull(waiter.await())
            assertEquals("Only owner cancellation retries automatically", 2, calls)
            if (status == CoverFetchResult.NOT_FOUND) {
                assertNull(cache.resolve(asset())); assertEquals(2, calls)
            } else {
                assertNotNull(cache.resolve(asset())); assertEquals(3, calls)
            }
        }
    } }
    @Test fun clearNeverDeletesCustomFilesOrSiblingPrefixPaths() = runBlocking { fixture { root ->
        val managed = File(root, "managed"); val custom = File(root, "managed-custom.webp").apply { writeBytes(valid) }
        stored(managed)
        val cache = CoverDiskCache(managed, { _, _, _ -> CoverFetchResult.RETRYABLE_FAILURE }, { true })
        assertFalse(cache.isManagedPath(custom.path))
        assertTrue(cache.isManagedPath(stored(managed).path))
        cache.clear()
        assertTrue(custom.isFile); assertTrue(managed.walk().none { it.isFile })
    } }
    @Test fun clearDuringDownloadPreventsLatePromotion() = runBlocking { fixture { root ->
        val entered = CompletableDeferred<Unit>(); val release = CompletableDeferred<Unit>()
        val cache = CoverDiskCache(root, { _, temp, _ ->
            entered.complete(Unit); release.await(); temp.writeBytes(valid); CoverFetchResult.DOWNLOADED
        }, { true })
        val pending = async { cache.resolve(asset()) }
        entered.await(); cache.clear(); release.complete(Unit)
        assertNull(pending.await()); assertTrue(root.walk().none { it.isFile })
    } }
    @Test fun flatAnd3dAreIndependentAssets() = runBlocking { fixture { root ->
        var calls = 0
        val cache = CoverDiskCache(root, { _, temp, _ -> calls++; temp.writeBytes(valid); CoverFetchResult.DOWNLOADED }, { true })
        val flat = cache.resolve(asset(VitaCoverStyle.FLAT))!!
        val threeD = cache.resolve(asset())!!
        assertNotEquals(flat.path, threeD.path); assertEquals(2, calls)
    } }
    @Test fun oversizedDownloadNeverPromotesEvenWhenFetcherClaimsSuccess() = runBlocking { fixture { root ->
        val cache = CoverDiskCache(root, { _, temp, _ ->
            java.io.RandomAccessFile(temp, "rw").use { it.setLength(CoverDiskCache.MAX_BYTES + 1) }
            CoverFetchResult.DOWNLOADED
        }, { error("oversized data must be rejected before image validation") })
        assertNull(cache.resolve(asset())); assertTrue(root.walk().none { it.isFile })
    } }
    @Test fun manyDistinctVisibleAssetsHaveAtMostFourConcurrentFetches() = runBlocking { fixture { root ->
        val active = AtomicInteger(); val peak = AtomicInteger()
        val cache = CoverDiskCache(root, { _, temp, _ ->
            val count = active.incrementAndGet(); peak.updateAndGet { maxOf(it, count) }
            try { delay(30); temp.writeBytes(valid); CoverFetchResult.DOWNLOADED }
            finally { active.decrementAndGet() }
        }, { true })
        val results = coroutineScope { (1L..16L).map { id -> async {
            cache.resolve(VitaCoverAsset(id, VitaCoverStyle.FLAT, "a".repeat(40), "covers/$id.webp", hash(valid)))
        } }.awaitAll() }
        assertTrue(results.all { it != null }); assertTrue(peak.get() in 2..4)
    } }

    @Test fun clearReportsOnlyDeletedManagedFilesAndActualBytes() = runBlocking { fixture { root ->
        val managed = File(root, "managed")
        stored(managed)
        val second = File(managed, "other/partial.tmp").apply { parentFile!!.mkdirs(); writeBytes(byteArrayOf(1, 2, 3)) }
        val custom = File(root, "custom.webp").apply { writeBytes(valid) }
        val cache = CoverDiskCache(managed, { _, _, _ -> error("clear must not fetch") }, { true })
        val result = cache.clear()
        assertEquals(2, result.deletedFiles)
        assertEquals(valid.size.toLong() + 3, result.freedBytes)
        assertEquals(0, result.failedFiles)
        assertTrue(result.fullyCleared)
        assertTrue(custom.isFile); assertFalse(second.exists())
        assertEquals(CoverCacheClearResult(0, 0, 0), cache.clear())
    } }

    @Test fun clearReportsPartialDeletionWithoutCountingFailedFileBytes() = runBlocking { fixture { root ->
        val keep = File(root, "keep.webp").apply { writeBytes(valid) }
        File(root, "remove.webp").writeBytes(byteArrayOf(1, 2))
        val directory = object : File(root.path) {
            override fun listFiles(): Array<File>? = super.listFiles()?.map { file ->
                if (file.name == keep.name) object : File(file.path) { override fun delete() = false } else file
            }?.toTypedArray()
        }
        val cache = CoverDiskCache(directory, { _, _, _ -> error("clear must not fetch") }, { true })
        assertEquals(CoverCacheClearResult(1, 2, 1), cache.clear())
        assertTrue(keep.isFile)
        assertFalse(cache.clear().fullyCleared)
    } }

    @Test fun clearReleasesOldWaiterAndNewFlightSurvivesOldOwnerCleanup() = runBlocking { fixture { root ->
        var calls = 0
        val firstEntered = CompletableDeferred<Unit>(); val firstCleaned = CompletableDeferred<Unit>()
        val finishOldCleanup = CompletableDeferred<Unit>()
        val newEntered = CompletableDeferred<Unit>(); val releaseNew = CompletableDeferred<Unit>()
        val cache = CoverDiskCache(root, { _, temp, _ ->
            when (++calls) {
                1 -> {
                    firstEntered.complete(Unit)
                    try { awaitCancellation() } finally {
                        withContext(NonCancellable) { finishOldCleanup.await(); firstCleaned.complete(Unit) }
                    }
                }
                else -> { newEntered.complete(Unit); releaseNew.await(); temp.writeBytes(valid); CoverFetchResult.DOWNLOADED }
            }
        }, { true }, ioDispatcher = Dispatchers.Unconfined)
        val oldOwner = async(start = CoroutineStart.UNDISPATCHED) { cache.resolve(asset()) }
        firstEntered.await()
        val oldWaiter = async(start = CoroutineStart.UNDISPATCHED) { cache.resolve(asset()) }
        cache.clear()
        assertNull(oldWaiter.await())
        val newOwner = async(start = CoroutineStart.UNDISPATCHED) { cache.resolve(asset()) }
        newEntered.await(); finishOldCleanup.complete(Unit); firstCleaned.await(); assertNull(oldOwner.await())
        val newWaiter = async(start = CoroutineStart.UNDISPATCHED) { cache.resolve(asset()) }
        assertFalse(newWaiter.isCompleted)
        releaseNew.complete(Unit)
        assertNotNull(newOwner.await()); assertNotNull(newWaiter.await())
        assertEquals(2, calls)
        assertTrue(root.walk().none { it.extension == "tmp" })
    } }

    @Test fun clearInvalidatesConfirmedMissSoNextRequestCanRetry() = runBlocking { fixture { root ->
        var calls = 0
        val cache = CoverDiskCache(root, { _, temp, _ ->
            if (++calls == 1) CoverFetchResult.NOT_FOUND
            else { temp.writeBytes(valid); CoverFetchResult.DOWNLOADED }
        }, { true })
        assertNull(cache.resolve(asset())); assertNull(cache.resolve(asset())); assertEquals(1, calls)
        cache.clear()
        assertNotNull(cache.resolve(asset())); assertEquals(2, calls)
    } }

    @Test fun clearDoesNotFollowSymbolicLinksToCustomArtwork() = runBlocking { fixture { root ->
        val managed = File(root, "managed").apply { mkdirs() }
        val custom = File(root, "private-custom").apply { mkdirs() }
        val original = File(custom, "original.webp").apply { writeBytes(valid) }
        val link = File(managed, "linked-custom")
        try { Files.createSymbolicLink(link.toPath(), custom.toPath()) }
        catch (unsupported: Exception) { org.junit.Assume.assumeNoException("Host cannot create symlinks", unsupported) }
        val cache = CoverDiskCache(managed, { _, _, _ -> error("clear must not fetch") }, { true })
        val result = cache.clear()
        assertEquals(CoverCacheClearResult(0, 0, 1), result)
        assertArrayEquals(valid, original.readBytes())
        Files.deleteIfExists(link.toPath())
    } }

    @Test fun unreadableManagedDirectoryIsReportedAsPartialInsteadOfSuccess() = runBlocking { fixture { root ->
        val directory = object : File(root.path) { override fun listFiles(): Array<File>? = null }
        val cache = CoverDiskCache(directory, { _, _, _ -> error("clear must not fetch") }, { true })
        assertEquals(CoverCacheClearResult(0, 0, 1), cache.clear())
    } }

    @Test fun clearRefusesAReplacedCacheRootSymlink() = runBlocking { fixture { root ->
        val custom = File(root, "private-originals").apply { mkdirs() }
        val original = File(custom, "original.webp").apply { writeBytes(valid) }
        val managed = File(root, "managed-link")
        try { Files.createSymbolicLink(managed.toPath(), custom.toPath()) }
        catch (unsupported: Exception) { org.junit.Assume.assumeNoException("Host cannot create symlinks", unsupported) }
        try {
            val cache = CoverDiskCache(managed, { _, _, _ -> error("clear must not fetch") }, { true })
            assertEquals(CoverCacheClearResult(0, 0, 1), cache.clear())
            assertArrayEquals(valid, original.readBytes())
        } finally { Files.deleteIfExists(managed.toPath()) }
    } }
}
