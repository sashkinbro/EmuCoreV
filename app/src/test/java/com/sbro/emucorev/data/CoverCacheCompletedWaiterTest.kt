package com.sbro.emucorev.data

import java.nio.file.Files
import java.security.MessageDigest
import java.util.concurrent.ConcurrentLinkedQueue
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.*
import org.junit.Assert.*
import org.junit.Test

class CoverCacheCompletedWaiterTest {
    private class QueuedDispatcher:CoroutineDispatcher() {
        private val queue=ConcurrentLinkedQueue<Runnable>()
        override fun dispatch(context:CoroutineContext,block:Runnable){queue.add(block)}
        fun runOne(){checkNotNull(queue.poll()){"Expected one queued continuation"}.run()}
    }
    @Test fun completedWaiterCannotReturnDeletedPreClearPath()=runBlocking {
        val root=Files.createTempDirectory("vita-cover-waiter-review-").toFile()
        val bytes="confirmed image fixture".toByteArray()
        val hash=MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
        val asset=VitaCoverAsset(42,VitaCoverStyle.FLAT,"a".repeat(40),"covers/42.webp",hash)
        val dispatcher=QueuedDispatcher()
        val fetchEntered=CompletableDeferred<Unit>();val fetchRelease=CompletableDeferred<Unit>()
        val cache=CoverDiskCache(root,{_,temp,_->
            fetchEntered.complete(Unit);fetchRelease.await();temp.writeBytes(bytes);CoverFetchResult.DOWNLOADED
        },{true},ioDispatcher=dispatcher)
        try {
            withTimeout(3000) {
                val owner=async(start=CoroutineStart.UNDISPATCHED){cache.resolve(asset)}
                dispatcher.runOne() // owner enrolls and awaits the lazy operation
                dispatcher.runOne() // operation starts and waits for fetch release
                assertTrue(fetchEntered.isCompleted)
                val waiter=async(start=CoroutineStart.UNDISPATCHED){cache.resolve(asset)}
                dispatcher.runOne() // waiter joins the existing flight
                fetchRelease.complete(Unit)
                dispatcher.runOne() // fetch completes, validates, promotes; owner resume is queued
                dispatcher.runOne() // owner completes Completed(file); waiter resume remains queued
                assertTrue(java.io.File(root,asset.cacheKey).isFile)
                cache.clear()
                assertFalse(java.io.File(root,asset.cacheKey).exists())
                dispatcher.runOne() // old waiter resumes only after clear has completed
                assertNull("Post-clear waiter must not return the now deleted pre-clear file",waiter.await())
                owner.await()
                Unit
            }
        } finally {root.deleteRecursively()}
    }
}
