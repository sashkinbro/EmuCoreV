package com.sbro.emucorev.data

import java.io.File
import java.nio.file.AtomicMoveNotSupportedException
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.security.MessageDigest
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicLong
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.async
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.isActive
import kotlinx.coroutines.supervisorScope
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull

enum class CoverFetchResult { DOWNLOADED, NOT_FOUND, RETRYABLE_FAILURE }
fun interface CoverAssetFetcher {
    suspend fun fetch(asset: VitaCoverAsset, destination: File, maxBytes: Long): CoverFetchResult
}

data class CoverCacheClearResult(val deletedFiles: Int, val freedBytes: Long, val failedFiles: Int) {
    val fullyCleared: Boolean get() = failedFiles == 0
}

/** Injected image validation permits deterministic JVM tests without an Android decoder. */
class CoverDiskCache(
    private val directory: File,
    private val fetcher: CoverAssetFetcher,
    private val imageValidator: (File) -> Boolean,
    private val now: () -> Long = System::currentTimeMillis,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO
) {
    private sealed interface FlightOutcome {
        data class Completed(val file: File?) : FlightOutcome
        data object OwnerCancelled : FlightOutcome
        data object CacheCleared : FlightOutcome
    }

    private class Flight(val generation: Long) {
        val result = CompletableDeferred<FlightOutcome>()
        var operation: Deferred<File?>? = null // guarded by mutationLock
    }
    private val flights = ConcurrentHashMap<String, Flight>()
    private val misses = ConcurrentHashMap<String, Long>()
    private class Failure(val count: Int, val at: Long)
    private data class Stamp(val length: Long, val modified: Long)
    /** Transient failures (B102): short, exponential back-off so offline scrolling cannot flood the network. */
    private val failures = ConcurrentHashMap<String, Failure>()
    /** Files already verified (hash + image) at this exact size/mtime (B103): not re-hashed per lookup. */
    private val verified = ConcurrentHashMap<String, Stamp>()
    private val permits = Semaphore(4)
    private val generation = AtomicLong()
    private val mutationLock = Any()

    suspend fun resolve(asset: VitaCoverAsset): File? = withContext(ioDispatcher) {
        findCached(asset) ?: resolveShared(asset)
    }

    /** Disk hits never queue behind network requests, and never initiate a fetch. */
    suspend fun findCached(asset: VitaCoverAsset): File? = withContext(ioDispatcher) {
        val startedGeneration = generation.get()
        val target = File(directory, asset.cacheKey)
        if (!isManagedPath(target.path)) return@withContext null
        if (!validManaged(target, asset.sha256)) return@withContext null
        target.takeIf { generation.get() == startedGeneration }
    }

    private suspend fun resolveShared(asset: VitaCoverAsset): File? = supervisorScope {
        while (true) {
            currentCoroutineContext().ensureActive()
            val (flight, ownsFlight) = synchronized(mutationLock) {
                flights[asset.cacheKey]?.let { it to false } ?: Flight(generation.get()).let {
                    flights[asset.cacheKey] = it
                    it to true
                }
            }
            if (!ownsFlight) {
                when (val outcome = flight.result.await()) {
                    is FlightOutcome.Completed -> return@supervisorScope synchronized(mutationLock) {
                        outcome.file.takeIf { generation.get() == flight.generation }
                    }
                    FlightOutcome.OwnerCancelled -> continue
                    FlightOutcome.CacheCleared -> return@supervisorScope null
                }
            }
            try {
                val operation = async(start = CoroutineStart.LAZY) {
                    permits.withPermit { resolveOnce(asset, flight.generation) }
                }
                synchronized(mutationLock) {
                    if (generation.get() == flight.generation && flights[asset.cacheKey] === flight) {
                        flight.operation = operation
                    } else operation.cancel()
                }
                val file = operation.await()
                val current = synchronized(mutationLock) {
                    val current = generation.get() == flight.generation
                    flight.result.complete(if (current) FlightOutcome.Completed(file) else FlightOutcome.CacheCleared)
                    current
                }
                return@supervisorScope file.takeIf { current }
            } catch (cancelled: CancellationException) {
                // A clear cancels only the owned operation, not its caller. Waiters have
                // already been released and new-generation flights must remain registered.
                if (currentCoroutineContext().isActive && generation.get() != flight.generation) {
                    flight.result.complete(FlightOutcome.CacheCleared)
                    return@supervisorScope null
                }
                // Caller cancellation still allows unrelated active waiters to take over,
                // after the operation's temporary-file and permit cleanup has completed.
                withContext(NonCancellable) {
                    flight.operation?.cancel()
                    flight.operation?.join()
                }
                flights.remove(asset.cacheKey, flight)
                flight.result.complete(FlightOutcome.OwnerCancelled)
                throw cancelled
            } catch (_: Exception) {
                flight.result.complete(FlightOutcome.Completed(null))
                return@supervisorScope null
            } finally {
                flights.remove(asset.cacheKey, flight)
            }
        }
        @Suppress("UNREACHABLE_CODE") null
    }

    private suspend fun resolveOnce(asset: VitaCoverAsset, startedGeneration: Long): File? {
        if (generation.get() != startedGeneration) return null
        val target = File(directory, asset.cacheKey)
        if (!isManagedPath(target.path)) return null
        if (validManaged(target, asset.sha256)) return target.takeIf { generation.get() == startedGeneration }
        val checkedAt = now()
        synchronized(mutationLock) {
            // An old request may resume after clear and a fresh confirmed miss.
            // Keep the clock outside this lock and every miss mutation generation-bound.
            if (generation.get() != startedGeneration) return null
            val missAge = misses[asset.cacheKey]?.let { checkedAt - it }
            if (missAge != null && missAge in 0 until MISS_TTL_MS) return null
            misses.remove(asset.cacheKey)
            val failure = failures[asset.cacheKey]
            val failureAge = failure?.let { checkedAt - it.at }
            if (failure != null && failureAge != null && failureAge in 0 until retryDelayMs(failure.count)) return null
        }
        return run {
            currentCoroutineContext().ensureActive()
            if (generation.get() != startedGeneration) return@run null
            target.parentFile?.mkdirs()
            val temp = File.createTempFile("asset-", ".tmp", target.parentFile)
            try {
                val status = withTimeoutOrNull(40_000) { fetcher.fetch(asset, temp, MAX_BYTES) }
                currentCoroutineContext().ensureActive()
                if (status == CoverFetchResult.NOT_FOUND) synchronized(mutationLock) {
                    if (generation.get() == startedGeneration) misses[asset.cacheKey] = now()
                }
                if (status != CoverFetchResult.DOWNLOADED || !valid(temp, asset.sha256)) {
                    // Only definitive 404/410 are negative-cached (30 min); everything else backs off briefly.
                    if (status != CoverFetchResult.NOT_FOUND) noteFailure(asset.cacheKey, startedGeneration)
                    return@run null
                }
                val stamp = Stamp(temp.length(), temp.lastModified())
                synchronized(mutationLock) {
                    if (generation.get() != startedGeneration) return@synchronized null
                    // Same-directory atomic promotion never exposes a partly downloaded file.
                    try {
                        Files.move(temp.toPath(), target.toPath(), StandardCopyOption.ATOMIC_MOVE,
                            StandardCopyOption.REPLACE_EXISTING)
                    } catch (_: AtomicMoveNotSupportedException) {
                        // Fail closed on filesystems which cannot provide atomic publication.
                        return@synchronized null
                    }
                    misses.remove(asset.cacheKey)
                    failures.remove(asset.cacheKey)
                    verified[target.path] = stamp
                    target
                }
            } finally {
                temp.delete()
            }
        }
    }

    private fun noteFailure(key: String, startedGeneration: Long) {
        val at = now()
        synchronized(mutationLock) {
            if (generation.get() != startedGeneration) return
            failures[key] = Failure((failures[key]?.count ?: 0) + 1, at)
        }
    }

    /** Connectivity regained: let every back-off expire so visible covers retry at once. */
    fun resetFailureBackoff() { failures.clear() }

    private suspend fun validManaged(file: File, expectedHash: String): Boolean {
        val stamp = Stamp(file.length(), file.lastModified())
        if (file.isFile && verified[file.path] == stamp) return true
        if (!valid(file, expectedHash)) { verified.remove(file.path); return false }
        verified[file.path] = stamp
        return true
    }

    private suspend fun valid(file: File, expectedHash: String): Boolean {
        if (!file.isFile || file.length() !in 1..MAX_BYTES) return false
        return try {
            val digest = MessageDigest.getInstance("SHA-256")
            file.inputStream().use { input ->
                val buffer = ByteArray(16 * 1024)
                var bytes = 0L
                while (true) {
                    currentCoroutineContext().ensureActive()
                    val length = input.read(buffer)
                    if (length < 0) break
                    bytes += length
                    if (bytes > MAX_BYTES) return false
                    digest.update(buffer, 0, length)
                }
            }
            val hash = digest.digest().joinToString("") { "%02x".format(it) }
            hash == expectedHash && imageValidator(file)
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: Exception) {
            false
        }
    }

    /** Only this dedicated root is cleared. Failed/uninspectable entries are reported. */
    suspend fun clear(): CoverCacheClearResult = withContext(Dispatchers.IO) {
        synchronized(mutationLock) {
            generation.incrementAndGet()
            misses.clear()
            failures.clear()
            verified.clear()
            val invalidated = flights.values.toList()
            flights.clear()
            invalidated.forEach { flight ->
                flight.result.complete(FlightOutcome.CacheCleared)
                flight.operation?.cancel()
            }
            var deleted = 0
            var bytes = 0L
            var failed = 0
            fun removeChildren(parent: File) {
                val entries = runCatching { parent.listFiles() }.getOrNull()
                if (entries == null) { failed++; return }
                entries.forEach { entry ->
                    // Do not traverse even an in-root link: it may alias/cycle another
                    // subtree, or point at private originals outside this cache.
                    if (Files.isSymbolicLink(entry.toPath()) || !isManagedPath(entry.path)) {
                        failed++
                    } else if (entry.isDirectory) {
                        removeChildren(entry)
                        runCatching { entry.delete() } // Empty directories carry no artwork bytes.
                    } else {
                        val size = entry.length()
                        if (runCatching { entry.delete() }.getOrDefault(false)) {
                            deleted++
                            bytes += size
                        } else if (entry.exists()) failed++
                    }
                }
            }
            when {
                Files.isSymbolicLink(directory.toPath()) -> failed++
                !directory.exists() -> Unit
                !directory.isDirectory -> failed++
                else -> removeChildren(directory)
            }
            CoverCacheClearResult(deleted, bytes, failed)
        }
    }

    fun isManagedPath(path: String): Boolean = runCatching {
        val root = directory.canonicalFile.toPath()
        val candidate = File(path).canonicalFile.toPath()
        candidate != root && candidate.startsWith(root)
    }.getOrDefault(false)

    companion object {
        const val MAX_BYTES: Long = 32L * 1024 * 1024
        private const val MISS_TTL_MS = 30L * 60 * 1000
        private const val RETRY_BASE_MS = 60_000L
        private const val RETRY_MAX_MS = 10L * 60 * 1000
        /** 60 s, 2 min, 4 min ... capped at 10 min. */
        internal fun retryDelayMs(failureCount: Int): Long =
            (RETRY_BASE_MS shl (failureCount - 1).coerceIn(0, 10)).coerceAtMost(RETRY_MAX_MS)
    }
}
